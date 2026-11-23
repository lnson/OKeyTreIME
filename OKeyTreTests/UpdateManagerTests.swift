// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

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
}

