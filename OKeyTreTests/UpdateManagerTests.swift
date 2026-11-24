// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import AppKit
import ApplicationServices
import CoreGraphics
import Foundation
import Testing
@testable import OKeyTre

@MainActor
struct UpdateManagerTests {

    @Test("AppVersion parses tags, normalizes trailing zeros, and compares prereleases")
    func versionParsingAndComparison() throws {
        let v01 = try #require(AppVersion("v0.1"))
        let v010 = try #require(AppVersion("0.1.0"))
        let v02 = try #require(AppVersion("V0.2+20261122"))
        let v02Beta1 = try #require(AppVersion("v0.2.0-beta.1"))
        let v02Beta2 = try #require(AppVersion("0.2.0-beta.2"))
        let v010Minor = try #require(AppVersion("0.10"))

        #expect(v01 == v010)
        #expect(v01 < v02Beta1)
        #expect(v02Beta1 < v02Beta2)
        #expect(v02Beta2 < v02)
        #expect(v02 < v010Minor)
        #expect(v02.description == "0.2")

        #expect(AppVersion("") == nil)
        #expect(AppVersion("v") == nil)
        #expect(AppVersion("1..0") == nil)
        #expect(AppVersion("1.0-") == nil)
        #expect(AppVersion("abc") == nil)
    }

    @Test("parseLatestRelease extracts newer GitHub release and .dmg asset")
    func parseNewerReleasePayload() throws {
        let json = """
        {
          "tag_name": "v0.2.1",
          "html_url": "https://github.com/lnson/OKeyTreIME/releases/tag/v0.2.1",
          "draft": false,
          "prerelease": false,
          "assets": [
            {
              "name": "checksums.txt",
              "browser_download_url": "https://github.com/lnson/OKeyTreIME/releases/download/v0.2.1/checksums.txt"
            },
            {
              "name": "OKeyTre-0.2.1.dmg",
              "browser_download_url": "https://objects.githubusercontent.com/github-production-release-asset/OKeyTre-0.2.1.dmg"
            }
          ]
        }
        """
        let current = try #require(AppVersion("0.1"))
        let release = try #require(
            try UpdateManager.parseLatestRelease(from: Data(json.utf8), currentVersion: current)
        )

        #expect(release.tagName == "v0.2.1")
        #expect(release.displayVersion == "0.2.1")
        #expect(release.releasePageURL.absoluteString == "https://github.com/lnson/OKeyTreIME/releases/tag/v0.2.1")
        #expect(
            release.dmgDownloadURL?.absoluteString
                == "https://objects.githubusercontent.com/github-production-release-asset/OKeyTre-0.2.1.dmg"
        )
    }

    @Test("parseLatestRelease ignores matching, older, draft, or prerelease payloads and untrusted URLs")
    func parseIgnoredPayloadsAndUntrustedURLs() throws {
        let current = try #require(AppVersion("0.2"))

        let sameVersionJSON = """
        {"tag_name": "v0.2.0", "html_url": "https://github.com/lnson/OKeyTreIME/releases/tag/v0.2.0"}
        """
        #expect(try UpdateManager.parseLatestRelease(from: Data(sameVersionJSON.utf8), currentVersion: current) == nil)

        let olderVersionJSON = """
        {"tag_name": "v0.1.9", "html_url": "https://github.com/lnson/OKeyTreIME/releases/tag/v0.1.9"}
        """
        #expect(try UpdateManager.parseLatestRelease(from: Data(olderVersionJSON.utf8), currentVersion: current) == nil)

        let draftJSON = """
        {"tag_name": "v0.3.0", "html_url": "https://github.com/lnson/OKeyTreIME/releases/tag/v0.3.0", "draft": true}
        """
        #expect(try UpdateManager.parseLatestRelease(from: Data(draftJSON.utf8), currentVersion: current) == nil)

        let untrustedURLJSON = """
        {
          "tag_name": "v0.3.0",
          "html_url": "file:///Applications/Calculator.app",
          "assets": [{"name": "OKeyTre.dmg", "browser_download_url": "http://evil.example.com/OKeyTre.dmg"}]
        }
        """
        let sanitized = try #require(
            try UpdateManager.parseLatestRelease(from: Data(untrustedURLJSON.utf8), currentVersion: current)
        )
        #expect(sanitized.releasePageURL == UpdateManager.releasesWebURL)
        #expect(sanitized.dmgDownloadURL == nil)
    }

    @Test("Background checks enforce 24-hour interval, toggle state, and skipped versions")
    func backgroundCheckScheduleAndSkipBehavior() async throws {
        let suiteName = "com.okeytre.tests.updates.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suiteName))
        defer { defaults.removePersistentDomain(forName: suiteName) }

        var simulatedNow = Date(timeIntervalSince1970: 1_700_000_000)
        var networkCallCount = 0
        var openedURLs: [URL] = []

        let payload = """
        {
          "tag_name": "v0.2.0",
          "html_url": "https://github.com/lnson/OKeyTreIME/releases/tag/v0.2.0"
        }
        """

        let current = try #require(AppVersion("0.1"))
        let manager = UpdateManager(
            userDefaults: defaults,
            currentVersion: current,
            isAutomatedEnvironment: true,
            dataLoader: { request in
                networkCallCount += 1
                let response = HTTPURLResponse(
                    url: request.url!,
                    statusCode: 200,
                    httpVersion: nil,
                    headerFields: nil
                )!
                return (Data(payload.utf8), response)
            },
            nowProvider: { simulatedNow },
            openURLHandler: { openedURLs.append($0) }
        )

        #expect(manager.isAutoCheckEnabled)
        #expect(manager.secondsUntilNextAutomaticCheck() == 0)

        // First due background check runs and surfaces v0.2.0.
        let firstRan = await manager.checkIfDueInBackground()
        #expect(firstRan)
        #expect(networkCallCount == 1)
        let release = try #require(manager.visibleAvailableRelease)
        #expect(release.displayVersion == "0.2.0")

        // 12 hours later: still throttled (< 24h).
        simulatedNow = simulatedNow.addingTimeInterval(12 * 60 * 60)
        #expect(manager.secondsUntilNextAutomaticCheck() == 12 * 60 * 60)
        let throttledRan = await manager.checkIfDueInBackground()
        #expect(!throttledRan)
        #expect(networkCallCount == 1)

        // Skipping v0.2.0 hides it from passive menu bar notification,
        // while a manual check surfaces it again.
        manager.skipRelease(release)
        #expect(manager.visibleAvailableRelease == nil)

        let manualRan = await manager.performCheck(userInitiated: true)
        #expect(manualRan)
        #expect(networkCallCount == 2)
        #expect(manager.visibleAvailableRelease?.displayVersion == "0.2.0")

        manager.openReleasePage()
        #expect(openedURLs == [URL(string: "https://github.com/lnson/OKeyTreIME/releases/tag/v0.2.0")!])

        // Disabling automatic checks prevents background checks even after 24 hours pass.
        manager.setAutoCheckEnabled(false)
        #expect(!defaults.bool(forKey: UpdateManager.autoCheckDefaultsKey))
        simulatedNow = simulatedNow.addingTimeInterval(25 * 60 * 60)
        let disabledRan = await manager.checkIfDueInBackground()
        #expect(!disabledRan)
        #expect(networkCallCount == 2)
    }

    @Test("404 response from GitHub Releases (no release published yet) succeeds cleanly")
    func handlesNoPublishedReleasesYet() async throws {
        let suiteName = "com.okeytre.tests.updates.404.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suiteName))
        defer { defaults.removePersistentDomain(forName: suiteName) }

        let fixedNow = Date(timeIntervalSince1970: 1_700_000_000)
        let current = try #require(AppVersion("0.1"))
        let manager = UpdateManager(
            userDefaults: defaults,
            currentVersion: current,
            isAutomatedEnvironment: true,
            dataLoader: { request in
                let response = HTTPURLResponse(
                    url: request.url!,
                    statusCode: 404,
                    httpVersion: nil,
                    headerFields: nil
                )!
                return (Data(), response)
            },
            nowProvider: { fixedNow }
        )

        let succeeded = await manager.performCheck(userInitiated: true)
        #expect(succeeded)
        #expect(manager.availableRelease == nil)
        #expect(manager.didConfirmUpToDate)
        #expect(manager.secondsUntilNextAutomaticCheck() == UpdateManager.checkInterval)
    }
}

@MainActor
struct AppDefaultsTests {

    @Test("modernTone defaults to true when unset and respects explicit UserDefaults override")
    func modernToneDefaultEnabled() throws {
        let suiteName = "com.okeytre.tests.moderntone.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suiteName))
        defer { defaults.removePersistentDomain(forName: suiteName) }

        #expect(InputMethodBuffer.defaultModernTone)
        #expect(InputMethodBuffer.resolvedModernTone(from: defaults))

        let defaultBuffer = InputMethodBuffer()
        #expect(defaultBuffer.modernTone)

        defaults.set(false, forKey: InputMethodBuffer.modernToneDefaultsKey)
        #expect(!InputMethodBuffer.resolvedModernTone(from: defaults))

        defaults.set(true, forKey: InputMethodBuffer.modernToneDefaultsKey)
        #expect(InputMethodBuffer.resolvedModernTone(from: defaults))
    }

    @Test("automaticallyCheckForUpdates defaults to true when unset and respects explicit UserDefaults override")
    func autoCheckUpdatesDefaultEnabled() throws {
        let suiteName = "com.okeytre.tests.autocheck.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suiteName))
        defer { defaults.removePersistentDomain(forName: suiteName) }

        defaults.register(defaults: [
            UpdateManager.autoCheckDefaultsKey: UpdateManager.defaultAutoCheck,
        ])

        #expect(UpdateManager.defaultAutoCheck)
        #expect(defaults.bool(forKey: UpdateManager.autoCheckDefaultsKey))
        #expect(UpdateManager.resolvedAutoCheckEnabled(from: defaults))

        let manager = UpdateManager(userDefaults: defaults, isAutomatedEnvironment: true)
        #expect(manager.isAutoCheckEnabled)

        manager.setAutoCheckEnabled(false)
        #expect(!defaults.bool(forKey: UpdateManager.autoCheckDefaultsKey))
        #expect(!UpdateManager.resolvedAutoCheckEnabled(from: defaults))
    }

    @Test("LaunchAtLoginManager enables by default once and preserves user opt-out across launches")
    func launchAtLoginDefaultAndOptOut() throws {
        let suiteName = "com.okeytre.tests.launchatlogin.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suiteName))
        defer { defaults.removePersistentDomain(forName: suiteName) }

        var serviceEnabled = false
        var registerCalls = 0
        var unregisterCalls = 0

        let makeBackend = {
            LaunchAtLoginManager.ServiceBackend(
                isEnabled: { serviceEnabled },
                requiresApproval: { false },
                register: {
                    registerCalls += 1
                    serviceEnabled = true
                },
                unregister: {
                    unregisterCalls += 1
                    serviceEnabled = false
                },
                openLoginItemsSettings: {}
            )
        }

        // First launch: enables Launch at Login automatically.
        let firstLaunchManager = LaunchAtLoginManager(
            userDefaults: defaults,
            backend: makeBackend(),
            observeAppActivation: false
        )
        #expect(!firstLaunchManager.isEnabled)
        firstLaunchManager.enableByDefaultIfNeeded()
        #expect(firstLaunchManager.isEnabled)
        #expect(registerCalls == 1)
        #expect(defaults.bool(forKey: LaunchAtLoginManager.hasConfiguredDefaultsKey))

        // User explicitly turns Launch at Login off.
        firstLaunchManager.setEnabled(false)
        #expect(!firstLaunchManager.isEnabled)
        #expect(unregisterCalls == 1)

        // Subsequent launch: respects user opt-out and does not re-register.
        let secondLaunchManager = LaunchAtLoginManager(
            userDefaults: defaults,
            backend: makeBackend(),
            observeAppActivation: false
        )
        secondLaunchManager.enableByDefaultIfNeeded()
        #expect(!secondLaunchManager.isEnabled)
        #expect(registerCalls == 1)
    }

    @Test("InputMethodStyle.off disables transforms, remembers last active style on toggle, and DualCommandDetector handles chords")
    func offInputMethodStyleAndDualCommandDetector() throws {
        let suiteName = "com.okeytre.tests.vietnamese.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suiteName))
        defer { defaults.removePersistentDomain(forName: suiteName) }

        let manager = KeyTapManager(defaults: defaults, observeWorkspace: false)
        #expect(manager.inputMethodStyle == .telex)
        #expect(manager.isVietnameseEnabled)

        // Select VNI, then toggle off via Left+Right Command action -> switches to .off,
        // and toggling again restores .vni!
        manager.setInputMethodStyle(.vni, defaults: defaults)
        #expect(manager.inputMethodStyle == .vni)

        manager.toggleVietnameseEnabled(defaults: defaults)
        #expect(manager.inputMethodStyle == .off)
        #expect(!manager.isVietnameseEnabled)

        // Reloading from UserDefaults while .off preserves .off AND remembers .vni as last active style.
        let reloadedManager = KeyTapManager(defaults: defaults, observeWorkspace: false)
        #expect(reloadedManager.inputMethodStyle == .off)
        reloadedManager.toggleVietnameseEnabled(defaults: defaults)
        #expect(reloadedManager.inputMethodStyle == .vni)
        #expect(reloadedManager.isVietnameseEnabled)

        // Verify InputMethodBuffer(.off) passes input through untouched.
        let offBuffer = InputMethodBuffer(style: .off)
        #expect(offBuffer.style == .off)
        let action: InputMethodBuffer.Action? = [UniChar(0x0061), UniChar(0x0073)].withUnsafeBufferPointer { ptr in
            _ = offBuffer.handleInput(UnsafeBufferPointer(start: ptr.baseAddress, count: 1))
            return offBuffer.handleInput(UnsafeBufferPointer(start: ptr.baseAddress! + 1, count: 1))
        }
        #expect(action == nil)

        var detector = DualCommandDetector()

        // 1. Clean Left Command down -> Right Command down triggers toggle.
        let leftDown1 = detector.shouldTriggerToggle(keyCode: KeyCodes.leftCommandKeyCode, flags: .maskCommand)
        #expect(!leftDown1)
        let rightDown1 = detector.shouldTriggerToggle(keyCode: KeyCodes.rightCommandKeyCode, flags: .maskCommand)
        #expect(rightDown1)

        // Releasing both Command keys resets cleanly.
        let rightUp1 = detector.shouldTriggerToggle(keyCode: KeyCodes.rightCommandKeyCode, flags: .maskCommand)
        #expect(!rightUp1)
        let leftUp1 = detector.shouldTriggerToggle(keyCode: KeyCodes.leftCommandKeyCode, flags: [])
        #expect(!leftUp1)

        // 2. Clean Right Command down -> Left Command down also triggers toggle.
        let rightDown2 = detector.shouldTriggerToggle(keyCode: KeyCodes.rightCommandKeyCode, flags: .maskCommand)
        #expect(!rightDown2)
        let leftDown2 = detector.shouldTriggerToggle(keyCode: KeyCodes.leftCommandKeyCode, flags: .maskCommand)
        #expect(leftDown2)
        let leftUp2 = detector.shouldTriggerToggle(keyCode: KeyCodes.leftCommandKeyCode, flags: .maskCommand)
        #expect(!leftUp2)
        let rightUp2 = detector.shouldTriggerToggle(keyCode: KeyCodes.rightCommandKeyCode, flags: [])
        #expect(!rightUp2)

        // 3. Pressing a regular key while holding Command (e.g. Cmd+C) disrupts the chord until released.
        let leftDown3 = detector.shouldTriggerToggle(keyCode: KeyCodes.leftCommandKeyCode, flags: .maskCommand)
        #expect(!leftDown3)
        detector.noteNonModifierKeyDown()
        let rightDownDisrupted = detector.shouldTriggerToggle(keyCode: KeyCodes.rightCommandKeyCode, flags: .maskCommand)
        #expect(!rightDownDisrupted)
        let rightUpDisrupted = detector.shouldTriggerToggle(keyCode: KeyCodes.rightCommandKeyCode, flags: .maskCommand)
        #expect(!rightUpDisrupted)
        let leftUpDisrupted = detector.shouldTriggerToggle(keyCode: KeyCodes.leftCommandKeyCode, flags: [])
        #expect(!leftUpDisrupted)

        // 4. Holding Shift/Option/Control alongside Command does not trigger toggle.
        let shiftLeftCmd = detector.shouldTriggerToggle(
            keyCode: KeyCodes.leftCommandKeyCode,
            flags: [.maskCommand, .maskShift]
        )
        #expect(!shiftLeftCmd)
        let shiftRightCmd = detector.shouldTriggerToggle(
            keyCode: KeyCodes.rightCommandKeyCode,
            flags: [.maskCommand, .maskShift]
        )
        #expect(!shiftRightCmd)
        detector.reset()
        let leftAfterReset = detector.shouldTriggerToggle(keyCode: KeyCodes.leftCommandKeyCode, flags: .maskCommand)
        #expect(!leftAfterReset)
        let rightAfterReset = detector.shouldTriggerToggle(keyCode: KeyCodes.rightCommandKeyCode, flags: .maskCommand)
        #expect(rightAfterReset)
    }

    @Test("DockIconCoordinator shows Dock icon while Settings is open, minimized, or hidden and hides on close")
    func dockIconActivationPolicyLifecycle() async throws {
        // Pure open-state resolution across visible, minimized, hidden, and closed states.
        #expect(
            !DockIconCoordinator.resolvedSettingsOpenState(
                isVisible: false,
                isMiniaturized: false,
                isAppHidden: false,
                wasOpen: false,
                isOpening: false
            )
        )
        #expect(
            DockIconCoordinator.resolvedSettingsOpenState(
                isVisible: true,
                isMiniaturized: false,
                isAppHidden: false,
                wasOpen: false,
                isOpening: false
            )
        )
        #expect(
            DockIconCoordinator.resolvedSettingsOpenState(
                isVisible: false,
                isMiniaturized: true,
                isAppHidden: false,
                wasOpen: false,
                isOpening: false
            )
        )
        #expect(
            DockIconCoordinator.resolvedSettingsOpenState(
                isVisible: false,
                isMiniaturized: false,
                isAppHidden: true,
                wasOpen: true,
                isOpening: false
            )
        )
        #expect(
            !DockIconCoordinator.resolvedSettingsOpenState(
                isVisible: false,
                isMiniaturized: false,
                isAppHidden: true,
                wasOpen: false,
                isOpening: false
            )
        )

        var currentPolicy: NSApplication.ActivationPolicy = .accessory
        var policyTransitions: [NSApplication.ActivationPolicy] = []
        var isAppHidden = false
        var activateCount = 0
        var tooltipRefreshCount = 0

        let coordinator = DockIconCoordinator(
            currentPolicyProvider: { currentPolicy },
            setPolicyHandler: { newPolicy in
                currentPolicy = newPolicy
                policyTransitions.append(newPolicy)
            },
            isAppHiddenProvider: { isAppHidden },
            activateAppHandler: { activateCount += 1 },
            onAccessoryRestored: { tooltipRefreshCount += 1 }
        )

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 320, height: 240),
            styleMask: [.titled, .closable, .miniaturizable],
            backing: .buffered,
            defer: true
        )
        window.isReleasedWhenClosed = false
        coordinator.registerSettingsWindow(window)

        // Initially closed -> stays .accessory.
        #expect(currentPolicy == .accessory)
        #expect(!coordinator.isSettingsWindowOpen)

        // Opening Settings switches to .regular and activates the app.
        coordinator.showSettingsWindow {
            window.orderFront(nil)
        }
        #expect(currentPolicy == .regular)
        #expect(coordinator.isSettingsWindowOpen)
        #expect(policyTransitions == [.regular])
        #expect(activateCount >= 1)

        // Hiding the app (Cmd+H) while Settings is open keeps Dock icon (.regular).
        isAppHidden = true
        window.orderOut(nil)
        coordinator.syncWithWindowState()
        #expect(currentPolicy == .regular)
        #expect(coordinator.isSettingsWindowOpen)

        // Clicking Dock icon while open/hidden activates app again.
        let countBeforeReopen = activateCount
        coordinator.handleDockReopen()
        #expect(activateCount == countBeforeReopen + 1)

        // Unhiding and closing the Settings window restores .accessory and refreshes menu bar tooltip.
        isAppHidden = false
        window.orderFront(nil)
        window.close()
        try await Task.sleep(for: .milliseconds(20))

        #expect(currentPolicy == .accessory)
        #expect(!coordinator.isSettingsWindowOpen)
        #expect(policyTransitions == [.regular, .accessory])
        #expect(tooltipRefreshCount == 1)
    }

    @Test("CaretTextReader guards invalid caret ranges, empty contenteditable blocks, and placeholders so Backspace on empty keeps Telex active")
    func caretTextReaderEmptyAndPlaceholderGuards() throws {
        // 1. Rejects missing, negative (kCFNotFound), and NSNotFound / CFIndex.max locations.
        #expect(!CaretTextReader.isValidCaretLocation(nil))
        #expect(!CaretTextReader.isValidCaretLocation(-1))
        #expect(!CaretTextReader.isValidCaretLocation(NSNotFound))
        #expect(!CaretTextReader.isValidCaretLocation(Int.max))
        #expect(CaretTextReader.isValidCaretLocation(0))
        #expect(CaretTextReader.isValidCaretLocation(10))

        // 2. Detects placeholder text even with trailing contenteditable <br> newlines.
        #expect(CaretTextReader.isPlaceholderMatch(value: "Ask Gemini", placeholder: "Ask Gemini"))
        #expect(CaretTextReader.isPlaceholderMatch(value: "Ask Gemini\n", placeholder: "Ask Gemini"))
        #expect(!CaretTextReader.isPlaceholderMatch(value: "bootj", placeholder: "Ask Gemini"))
        #expect(!CaretTextReader.isPlaceholderMatch(value: "Ask Gemini", placeholder: nil))
        #expect(!CaretTextReader.isPlaceholderMatch(value: "Ask Gemini", placeholder: ""))

        // 3. Treats empty rich-text paragraph nodes (<br>, U+200B, U+FFFC) as empty while keeping real words.
        #expect(CaretTextReader.isEmptyCaretContainerText(""))
        #expect(CaretTextReader.isEmptyCaretContainerText("\n"))
        #expect(CaretTextReader.isEmptyCaretContainerText("\u{200B}\n"))
        #expect(CaretTextReader.isEmptyCaretContainerText("\u{FFFC}"))
        #expect(!CaretTextReader.isEmptyCaretContainerText("b"))
        #expect(!CaretTextReader.isEmptyCaretContainerText("Gemini"))

        // 4. Detects empty rich-text editor DOM class lists (e.g. Gemini's Quill `ql-blank`,
        //    TipTap `is-editor-empty` / leaf `is-empty`, CKEditor `ck-placeholder`) whose
        //    `AXValue` contains CSS `::before` placeholder text before the first character is typed.
        #expect(CaretTextReader.isEmptyEditorClassList(["ql-editor", "ql-blank", "textarea"]))
        #expect(!CaretTextReader.isEmptyEditorClassList(["ql-editor", "textarea"]))
        #expect(CaretTextReader.isEmptyEditorClassList(["ProseMirror", "is-editor-empty"]))
        #expect(CaretTextReader.isEmptyEditorClassList(["ck", "ck-placeholder"]))
        #expect(!CaretTextReader.isEmptyEditorClassList(["is-empty"], includeLeafClasses: false))
        #expect(CaretTextReader.isEmptyEditorClassList(["is-empty"], includeLeafClasses: true))

        // 5. Extracts start AXTextMarker in-process via HIServices (`AXTextMarkerRangeCopyStartMarker`)
        //    so Chromium/Firefox `AXSelectedTextMarkerRange` works without WebKit-only attributes.
        let startBytes: [UInt8] = [0x01, 0x02, 0x03, 0x04]
        let endBytes: [UInt8] = [0x05, 0x06, 0x07, 0x08]
        let startMarker = try startBytes.withUnsafeBufferPointer { ptr in
            let base = try #require(ptr.baseAddress)
            return AXTextMarkerCreate(kCFAllocatorDefault, base, ptr.count)
        }
        let endMarker = try endBytes.withUnsafeBufferPointer { ptr in
            let base = try #require(ptr.baseAddress)
            return AXTextMarkerCreate(kCFAllocatorDefault, base, ptr.count)
        }
        let markerRange = AXTextMarkerRangeCreate(kCFAllocatorDefault, startMarker, endMarker)
        let extractedStart = try #require(CaretTextReader.startTextMarker(for: markerRange))
        #expect(CFEqual(extractedStart, startMarker))
        #expect(!CFEqual(extractedStart, endMarker))
        #expect(CaretTextReader.startTextMarker(for: "not-a-marker-range" as CFString) == nil)

        // 6. Pressing Backspace when InputMethodBuffer is already empty and then typing "bootj"
        //    without poisoned placeholder seeding transforms "bootj" -> "bột".
        let buffer = InputMethodBuffer(style: .telex)
        let backspace: [UniChar] = [0x007F]
        let extraBackspaceAction = backspace.withUnsafeBufferPointer { ptr in
            buffer.handleInput(ptr)
        }
        #expect(extraBackspaceAction == nil)

        var rendered = ""
        for scalar in "bootj".unicodeScalars {
            let unit: [UniChar] = [UniChar(scalar.value)]
            let action = unit.withUnsafeBufferPointer { ptr in
                buffer.handleInput(ptr)
            }
            if let action {
                if action.backspaceCount > 0 {
                    let keepCount = max(0, rendered.utf16.count - action.backspaceCount)
                    let prefixUnits = Array(rendered.utf16.prefix(keepCount))
                    rendered = String(utf16CodeUnits: prefixUnits, count: prefixUnits.count)
                }
                rendered += action.text
            } else {
                rendered.unicodeScalars.append(scalar)
            }
        }
        #expect(rendered == "bột")
    }

    @Test("restoreInvalidWords defaults to true, respects UserDefaults overrides, and restores impossible Vietnamese words on word end")
    func restoreInvalidWordsLifecycleAndTyping() throws {
        let suiteName = "com.okeytre.tests.restoreinvalid.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suiteName))
        defer { defaults.removePersistentDomain(forName: suiteName) }

        #expect(InputMethodBuffer.defaultRestoreInvalidWords)
        #expect(InputMethodBuffer.resolvedRestoreInvalidWords(from: defaults))

        let buffer = InputMethodBuffer(style: .telex)
        #expect(buffer.restoreInvalidWords)

        func simulateTyping(_ input: String, into target: InputMethodBuffer) -> String {
            var screen: [UniChar] = []
            for ch in input.utf16 {
                let action = [ch].withUnsafeBufferPointer { ptr in
                    target.handleInput(ptr)
                }
                if let action {
                    if action.backspaceCount > 0 {
                        screen.removeLast(action.backspaceCount)
                    }
                    screen.append(contentsOf: action.text.utf16)
                } else {
                    screen.append(ch)
                }
            }
            return String(utf16CodeUnits: screen, count: screen.count)
        }

        // Enabled: English words with impossible Vietnamese phonotactics restore on Space/punctuation,
        // while valid Vietnamese words stay accented.
        let typedEnabled = simulateTyping("moojt window mowis cho user, lexi! ", into: buffer)
        #expect(typedEnabled == "một window mới cho user, lexi! ")

        // Disabled via UserDefaults / property: keeps transformed characters as-is.
        defaults.set(false, forKey: InputMethodBuffer.restoreInvalidWordsDefaultsKey)
        #expect(!InputMethodBuffer.resolvedRestoreInvalidWords(from: defaults))

        let manager = KeyTapManager(defaults: defaults, observeWorkspace: false)
        #expect(!manager.restoreInvalidWords)

        manager.setRestoreInvalidWords(true, defaults: defaults)
        #expect(manager.restoreInvalidWords)
        #expect(defaults.bool(forKey: InputMethodBuffer.restoreInvalidWordsDefaultsKey))

        buffer.reset()
        buffer.restoreInvalidWords = false
        let typedDisabled = simulateTyping("lexi ", into: buffer)
        #expect(typedDisabled == "lẽi ")
    }

    @Test("ShowcaseManager presents on first install and feature-milestone upgrades, skips bug-fix releases, and coordinates Dock icon")
    func firstLaunchShowcaseAndDockLifecycle() async throws {
        let suiteName = "com.okeytre.tests.showcase.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suiteName))
        defer { defaults.removePersistentDomain(forName: suiteName) }

        var presentedModes: [ShowcaseManager.PresentationMode] = []

        // 1. Fresh install on milestone 0.3 -> presents `.firstInstall` once and records "0.3".
        let initialShowcase = ShowcaseManager(
            defaults: defaults,
            milestoneVersionString: "0.3"
        ) { mode in
            presentedModes.append(mode)
        }
        #expect(initialShowcase.resolveLaunchPresentation() == .firstInstall)
        #expect(initialShowcase.presentOnLaunchIfNeeded())
        #expect(presentedModes == [.firstInstall])
        #expect(defaults.string(forKey: ShowcaseManager.lastSeenShowcaseVersionDefaultsKey) == "0.3")

        // 2. Subsequent launch or pure bug-fix release (e.g. app 0.3.1 keeping milestone "0.3") -> skipped!
        let bugfixShowcase = ShowcaseManager(
            defaults: defaults,
            milestoneVersionString: "0.3"
        ) { mode in
            presentedModes.append(mode)
        }
        #expect(!bugfixShowcase.shouldPresentOnLaunch())
        #expect(!bugfixShowcase.presentOnLaunchIfNeeded())
        #expect(presentedModes.count == 1)

        // 3. New feature release bumping milestone to "0.4" -> presents `.featureUpdate(version: "0.4")` once!
        let featureUpgradeShowcase = ShowcaseManager(
            defaults: defaults,
            milestoneVersionString: "0.4"
        ) { mode in
            presentedModes.append(mode)
        }
        #expect(featureUpgradeShowcase.resolveLaunchPresentation() == .featureUpdate(version: "0.4"))
        #expect(featureUpgradeShowcase.presentOnLaunchIfNeeded())
        #expect(presentedModes == [.firstInstall, .featureUpdate(version: "0.4")])
        #expect(defaults.string(forKey: ShowcaseManager.lastSeenShowcaseVersionDefaultsKey) == "0.4")
        #expect(!featureUpgradeShowcase.presentOnLaunchIfNeeded())

        // 4. Upgrading from a pre-showcase release (e.g. 0.2.1 with existing UserDefaults keys) -> `.featureUpdate`.
        let legacySuite = "com.okeytre.tests.showcase.legacy.\(UUID().uuidString)"
        let legacyDefaults = try #require(UserDefaults(suiteName: legacySuite))
        defer { legacyDefaults.removePersistentDomain(forName: legacySuite) }
        legacyDefaults.set(true, forKey: LaunchAtLoginManager.hasConfiguredDefaultsKey)

        let legacyUpgradeShowcase = ShowcaseManager(
            defaults: legacyDefaults,
            milestoneVersionString: "0.3"
        )
        #expect(legacyUpgradeShowcase.resolveLaunchPresentation() == .featureUpdate(version: "0.3"))

        // 5. DockIconCoordinator keeps .regular while Showcase window is open and restores .accessory on close.
        var currentPolicy: NSApplication.ActivationPolicy = .accessory
        let coordinator = DockIconCoordinator(
            currentPolicyProvider: { currentPolicy },
            setPolicyHandler: { currentPolicy = $0 },
            isAppHiddenProvider: { false },
            activateAppHandler: {},
            onAccessoryRestored: {}
        )

        let showcaseWindow = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 620, height: 545),
            styleMask: [.titled, .closable, .miniaturizable],
            backing: .buffered,
            defer: true
        )
        showcaseWindow.isReleasedWhenClosed = false
        coordinator.registerShowcaseWindow(showcaseWindow)

        coordinator.showShowcaseWindow {
            showcaseWindow.orderFront(nil)
        }
        #expect(currentPolicy == .regular)
        #expect(coordinator.isShowcaseWindowOpen)

        showcaseWindow.close()
        try await Task.sleep(for: .milliseconds(20))
        #expect(currentPolicy == .accessory)
        #expect(!coordinator.isShowcaseWindowOpen)
    }

    @Test("KeyTapManager suspends typing transforms and word reversion when system IME or non-English keyboard is active")
    func suspendsWhenSystemInputSourceIsNonEnglish() throws {
        // 1. Verify InputSourceManager classifies English keyboard layouts vs system IMEs.
        #expect(InputSourceManager.isCompatibleEnglishKeyboard(
            sourceID: "com.apple.keylayout.US",
            sourceType: "TISTypeKeyboardLayout"
        ))
        #expect(InputSourceManager.isCompatibleEnglishKeyboard(
            sourceID: "com.apple.keylayout.ABC",
            sourceType: "TISTypeKeyboardLayout"
        ))
        #expect(InputSourceManager.isCompatibleEnglishKeyboard(
            sourceID: "com.apple.keylayout.British",
            sourceType: "TISTypeKeyboardLayout"
        ))
        // macOS built-in Vietnamese IME modes & layout must NOT be treated as compatible English layouts.
        #expect(!InputSourceManager.isCompatibleEnglishKeyboard(
            sourceID: "com.apple.inputmethod.VietnameseIM.VietnameseTelex",
            sourceType: "TISTypeKeyboardInputMode"
        ))
        #expect(!InputSourceManager.isCompatibleEnglishKeyboard(
            sourceID: "com.apple.inputmethod.VietnameseIM.VietnameseVNI",
            sourceType: "TISTypeKeyboardInputMode"
        ))
        #expect(!InputSourceManager.isCompatibleEnglishKeyboard(
            sourceID: "com.apple.keylayout.Vietnamese",
            sourceType: "TISTypeKeyboardLayout"
        ))
        // Even an input mode whose ID contains "English" is still an input method, not a raw layout.
        #expect(!InputSourceManager.isCompatibleEnglishKeyboard(
            sourceID: "com.example.inputmethod.CustomEnglishMode",
            sourceType: "TISTypeKeyboardInputMode"
        ))

        // 2. Verify KeyTapManager passes keystrokes through untouched (no transforms, no auto-restore
        //    reversion, no Dual-Shift restore) while system IME is active, and resumes cleanly on U.S.
        let suiteName = "com.okeytre.tests.systemime.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suiteName))
        defer { defaults.removePersistentDomain(forName: suiteName) }

        var systemIsEnglish = true
        let manager = KeyTapManager(
            defaults: defaults,
            observeWorkspace: false,
            isEnglishInputSource: { systemIsEnglish }
        )
        #expect(manager.isVietnameseEnabled)
        #expect(manager.isEffectivelyActive)

        func typeThroughManager(_ input: String) -> String {
            var screen: [UniChar] = []
            for ch in input.utf16 {
                let action = [ch].withUnsafeBufferPointer { ptr in
                    manager.processKeyDown(ptr)
                }
                if let action {
                    if action.backspaceCount > 0 {
                        screen.removeLast(min(action.backspaceCount, screen.count))
                    }
                    screen.append(contentsOf: action.text.utf16)
                } else {
                    screen.append(ch)
                }
            }
            return String(utf16CodeUnits: screen, count: screen.count)
        }

        // Active on English layout: Telex transforms Vietnamese and auto-restores non-Vietnamese.
        #expect(typeThroughManager("tieengs user ") == "tiếng user ")

        // Start a word ("rồ "), then switch to system Vietnamese IME before Dual-Shift or next word:
        #expect(typeThroughManager("roof ") == "rồ ")
        systemIsEnglish = false
        #expect(manager.isVietnameseEnabled)
        #expect(!manager.isEffectivelyActive)

        // Dual-Shift must NOT revert the previously typed word when system IME is active.
        _ = manager.processFlagsChanged(keyCode: KeyCodes.leftShiftKeyCode, flags: .maskShift)
        let blockedRestore = manager.processFlagsChanged(
            keyCode: KeyCodes.rightShiftKeyCode,
            flags: .maskShift
        )
        #expect(blockedRestore == nil)

        // Typing while system IME is active produces zero actions (never transforms or reverts text).
        for ch in "tieengs user, window ".utf16 {
            let action = [ch].withUnsafeBufferPointer { ptr in
                manager.processKeyDown(ptr)
            }
            #expect(action == nil)
        }

        // Switching back to English keyboard automatically resumes Telex without toggling settings.
        systemIsEnglish = true
        #expect(manager.isEffectivelyActive)
        #expect(typeThroughManager("vieetj ") == "việt ")
    }
}

