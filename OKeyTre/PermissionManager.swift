// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import AppKit
import ApplicationServices
import Combine
import CoreFoundation
import CoreGraphics
import Darwin
import Foundation
import os

final class PermissionManager: ObservableObject {
    static let shared = PermissionManager()

    private nonisolated static let probeBundleIds = [
        "com.apple.dock",
        "com.apple.controlcenter",
        "com.apple.finder",
    ]

    private nonisolated static let systemSettingsBundleIds: Set<String> = [
        "com.apple.systempreferences",
        "com.apple.Settings",
    ]

    private struct MonitorSnapshot {
        var probePids: [pid_t] = []
        var isSystemSettingsRunning = false
        var isGranted = false
    }

    @Published private(set) var isAccessibilityGranted: Bool = false

    private let snapshotLock = OSAllocatedUnfairLock(initialState: MonitorSnapshot())
    private let monitorQueue = DispatchQueue(
        label: "com.okeytre.OKeyTre.permission-monitor",
        qos: .userInitiated
    )
    private var monitorTimer: DispatchSourceTimer?
    private var showSuccessAlertOnGrant = false
    private let isAutomatedEnvironment: Bool

    private init() {
        let env = ProcessInfo.processInfo.environment
        self.isAutomatedEnvironment = env["XCODE_RUNNING_FOR_PREVIEWS"] == "1"
            || env["XCTestConfigurationFilePath"] != nil
            || env["XCTestBundlePath"] != nil
            || env["XCTestSessionIdentifier"] != nil

        guard !isAutomatedEnvironment else { return }

        refreshWorkspaceSnapshot()
        let initialTrusted = Self.checkLiveAccessibilityTrusted(
            probePids: currentProbePids(),
            includeTapProbe: true
        )
        self.isAccessibilityGranted = initialTrusted
        snapshotLock.withLock { $0.isGranted = initialTrusted }

        observeSystemPermissionNotifications()
        observeWorkspaceNotifications()
        startBackgroundMonitor()
    }

    /// Synchronously verifies live `WindowServer` + cross-process Accessibility authorization
    /// without relying on `AXIsProcessTrusted()`'s stale per-process cache.
    @discardableResult
    func verifyPermissionNow() -> Bool {
        guard !isAutomatedEnvironment else { return isAccessibilityGranted }
        refreshWorkspaceSnapshot()
        let trusted = Self.checkLiveAccessibilityTrusted(
            probePids: currentProbePids(),
            includeTapProbe: true
        )
        if !trusted {
            KeyTapManager.shared.tearDownTapPortImmediately()
        }
        applyLivePermissionState(trusted)
        return isAccessibilityGranted
    }

    func requestAccessibility() {
        if verifyPermissionNow() {
            print("Accessibility allowed. Activating input method")
            return
        }
        promptAccessibilityRegistration()
    }

    func openAccessibilitySettings() {
        showSuccessAlertOnGrant = true
        promptAccessibilityRegistrationInFreshInstance {
            guard let url = URL(
                string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"
            ) else {
                return
            }
            // Only open System Settings directly if macOS suppressed its own native
            // `universalAccessAuthWarn` prompt (i.e. when an entry already exists in TCC).
            if !Self.isSystemAccessibilityPromptVisible() {
                NSWorkspace.shared.open(url)
            }
        }
    }

    private func applyLivePermissionState(_ trusted: Bool) {
        if trusted {
            let tapStarted = KeyTapManager.shared.start()
            snapshotLock.withLock { $0.isGranted = tapStarted }
            let wasGranted = isAccessibilityGranted
            if isAccessibilityGranted != tapStarted {
                isAccessibilityGranted = tapStarted
            }
            if tapStarted && !wasGranted && showSuccessAlertOnGrant {
                showSuccessAlertOnGrant = false
                let lang = AppLanguage.current
                let alert = NSAlert()
                alert.messageText = lang.localized("alert.accessibility_granted.title")
                alert.informativeText = lang.localized("alert.accessibility_granted.message")
                alert.alertStyle = .informational
                alert.runModal()
            }
        } else {
            snapshotLock.withLock { $0.isGranted = false }
            if isAccessibilityGranted {
                isAccessibilityGranted = false
            }
            KeyTapManager.shared.stop()
            CaretTextReader.resetEnhancedPids()
        }
    }

    private func currentProbePids() -> [pid_t] {
        snapshotLock.withLock { $0.probePids }
    }

    private func refreshWorkspaceSnapshot() {
        let currentPid = ProcessInfo.processInfo.processIdentifier
        let pids: [pid_t] = Self.probeBundleIds.compactMap { bundleId in
            NSRunningApplication.runningApplications(withBundleIdentifier: bundleId)
                .first(where: { !$0.isTerminated && $0.processIdentifier != currentPid })?
                .processIdentifier
        }

        let settingsOpen = Self.systemSettingsBundleIds.contains { bundleId in
            NSRunningApplication.runningApplications(withBundleIdentifier: bundleId)
                .contains { !$0.isTerminated }
        }

        snapshotLock.withLock { state in
            state.probePids = pids
            state.isSystemSettingsRunning = settingsOpen
        }
    }

    private func startBackgroundMonitor() {
        guard monitorTimer == nil else { return }
        let snapshotLock = self.snapshotLock
        let timer = DispatchSource.makeTimerSource(queue: monitorQueue)
        timer.schedule(
            deadline: .now() + .milliseconds(300),
            repeating: .milliseconds(300),
            leeway: .milliseconds(50)
        )
        timer.setEventHandler { [weak self] in
            Self.performBackgroundCheck(owner: self, snapshotLock: snapshotLock, forceTapProbe: false)
        }
        timer.resume()
        monitorTimer = timer
    }

    private func triggerImmediateBackgroundChecks() {
        let snapshotLock = self.snapshotLock
        monitorQueue.async { [weak self] in
            Self.performBackgroundCheck(owner: self, snapshotLock: snapshotLock, forceTapProbe: true)
        }
        monitorQueue.asyncAfter(deadline: .now() + .milliseconds(150)) { [weak self] in
            Self.performBackgroundCheck(owner: self, snapshotLock: snapshotLock, forceTapProbe: true)
        }
    }

    private nonisolated static func performBackgroundCheck(
        owner: PermissionManager?,
        snapshotLock: OSAllocatedUnfairLock<MonitorSnapshot>,
        forceTapProbe: Bool
    ) {
        guard owner != nil else { return }
        let snapshot = snapshotLock.withLock { $0 }
        let includeTapProbe = forceTapProbe || !snapshot.isGranted || snapshot.isSystemSettingsRunning
        let trusted = checkLiveAccessibilityTrusted(
            probePids: snapshot.probePids,
            includeTapProbe: includeTapProbe
        )

        if snapshot.isGranted && !trusted {
            // Tear down the Mach port directly on the background queue so `WindowServer`
            // releases the event tap immediately even if the main run loop is busy.
            KeyTapManager.tearDownSharedTapPortImmediately()
            snapshotLock.withLock { $0.isGranted = false }
            Task { @MainActor [weak owner] in
                owner?.applyLivePermissionState(false)
            }
        } else if !snapshot.isGranted && trusted {
            Task { @MainActor [weak owner] in
                owner?.applyLivePermissionState(true)
            }
        }
    }

    /// Checks live authorization against both cross-process `HIServices` Accessibility IPC and
    /// `WindowServer` (`CGEvent.tapCreate`), bypassing `AXIsProcessTrusted()`'s per-process cache
    /// (which stays stale `true` when an app is removed via `–` in System Settings).
    private nonisolated static func checkLiveAccessibilityTrusted(
        probePids: [pid_t],
        includeTapProbe: Bool
    ) -> Bool {
        guard canQuerySystemAccessibility(pids: probePids) else {
            return false
        }
        if includeTapProbe && !canCreateActiveKeyTap() {
            return false
        }
        return true
    }

    private nonisolated static func canCreateActiveKeyTap() -> Bool {
        let probeMask = CGEventMask(1 << CGEventType.keyUp.rawValue)
        guard let probeTap = CGEvent.tapCreate(
            tap: .cgSessionEventTap,
            place: .tailAppendEventTap,
            options: .defaultTap,
            eventsOfInterest: probeMask,
            callback: { _, _, event, _ in Unmanaged.passUnretained(event) },
            userInfo: nil
        ) else {
            return false
        }
        CGEvent.tapEnable(tap: probeTap, enable: false)
        CFMachPortInvalidate(probeTap)
        return true
    }

    private nonisolated static func canQuerySystemAccessibility(pids: [pid_t]) -> Bool {
        var testedAny = false
        for pid in pids where pid > 0 && kill(pid, 0) == 0 {
            testedAny = true
            let appElement = AXUIElementCreateApplication(pid)
            AXUIElementSetMessagingTimeout(appElement, 0.04)
            var roleRef: CFTypeRef?
            let err = AXUIElementCopyAttributeValue(
                appElement,
                kAXRoleAttribute as CFString,
                &roleRef
            )
            if err == .success {
                return true
            }
            if err == .apiDisabled {
                return false
            }
        }
        // If running system processes (Dock, ControlCenter, Finder) all rejected the query
        // (e.g. `.cannotComplete` `-25204` after TCC row deletion), cross-process AX is revoked.
        return !testedAny
    }

    nonisolated static func runAccessibilityRegistrationHelperAndExit() -> Never {
        let options = [
            kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true
        ] as CFDictionary
        _ = AXIsProcessTrustedWithOptions(options)
        _ = RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.25))
        Darwin._exit(0)
    }

    private func promptAccessibilityRegistration() {
        let options = [
            kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true
        ] as CFDictionary
        _ = AXIsProcessTrustedWithOptions(options)
    }

    /// Spawns a short-lived LaunchServices instance of `OKeyTre.app` with `--register-accessibility`
    /// so `AXIsProcessTrustedWithOptions(prompt: true)` executes with a fresh `launchd` audit token
    /// and uncached `HIServices` trust state (re-inserting the app into System Settings → Accessibility
    /// even if the user removed `–` the row while this process was already running).
    private func promptAccessibilityRegistrationInFreshInstance(
        completion: @escaping @MainActor () -> Void
    ) {
        promptAccessibilityRegistration()

        let bundleURL = Bundle.main.bundleURL
        guard bundleURL.pathExtension == "app" else {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
                completion()
            }
            return
        }

        let config = NSWorkspace.OpenConfiguration()
        config.createsNewApplicationInstance = true
        config.activates = false
        config.addsToRecentItems = false
        config.arguments = [OKeyTreMain.registerAccessibilityFlag]

        NSWorkspace.shared.openApplication(at: bundleURL, configuration: config) { _, _ in
            Task { @MainActor in
                try? await Task.sleep(for: .milliseconds(400))
                completion()
            }
        }
    }

    private static func isSystemAccessibilityPromptVisible() -> Bool {
        let authApps = NSRunningApplication.runningApplications(
            withBundleIdentifier: "com.apple.accessibility.universalAccessAuthWarn"
        )
        let authPids = Set(authApps.filter { !$0.isTerminated }.map(\.processIdentifier))
        guard !authPids.isEmpty,
              let windowList = CGWindowListCopyWindowInfo(
                  [.optionOnScreenOnly, .excludeDesktopElements],
                  kCGNullWindowID
              ) as? [[String: Any]] else {
            return false
        }
        return windowList.contains { info in
            guard let ownerPid = info[kCGWindowOwnerPID as String] as? pid_t else {
                return false
            }
            return authPids.contains(ownerPid)
        }
    }

    private func observeWorkspaceNotifications() {
        let workspaceCenter = NSWorkspace.shared.notificationCenter
        let names: [NSNotification.Name] = [
            NSWorkspace.didLaunchApplicationNotification,
            NSWorkspace.didTerminateApplicationNotification,
        ]
        for name in names {
            workspaceCenter.addObserver(
                forName: name,
                object: nil,
                queue: .main
            ) { [weak self] _ in
                self?.refreshWorkspaceSnapshot()
            }
        }
    }

    private func observeSystemPermissionNotifications() {
        let observer = Unmanaged.passUnretained(self).toOpaque()
        let callback: CFNotificationCallback = { _, observer, _, _, _ in
            guard let observer else { return }
            let manager = Unmanaged<PermissionManager>.fromOpaque(observer).takeUnretainedValue()
            Task { @MainActor in
                manager.refreshWorkspaceSnapshot()
                manager.triggerImmediateBackgroundChecks()
            }
        }

        CFNotificationCenterAddObserver(
            CFNotificationCenterGetDistributedCenter(),
            observer,
            callback,
            "com.apple.accessibility.api" as CFString,
            nil,
            .deliverImmediately
        )

        CFNotificationCenterAddObserver(
            CFNotificationCenterGetDarwinNotifyCenter(),
            observer,
            callback,
            "com.apple.tcc.access.changed" as CFString,
            nil,
            .deliverImmediately
        )
    }
}
