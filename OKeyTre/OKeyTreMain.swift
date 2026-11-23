// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import AppKit
import Darwin
import SwiftUI
import os

struct MenuBarStatusView: View {
    @ObservedObject var inputSourceManager = InputSourceManager.shared
    @ObservedObject var permissionManager = PermissionManager.shared

    var body: some View {
        let hasWarning = !permissionManager.isAccessibilityGranted || !inputSourceManager.isEnglish
        let iconName = hasWarning ? "exclamationmark.triangle.fill" : "keyboard"
        Image(systemName: iconName)
            .id(iconName)
    }
}

private struct MenuBarMenuContent: View {
    @ObservedObject var inputSourceManager = InputSourceManager.shared
    @ObservedObject var permissionManager = PermissionManager.shared
    @ObservedObject var launchAtLoginManager = LaunchAtLoginManager.shared
    @ObservedObject var updateManager = UpdateManager.shared
    @AppStorage(InputMethodBuffer.inputMethodDefaultsKey) private var selectedStyle: InputMethodStyle = .telex
    @AppStorage(AppLanguage.defaultsKey) private var appLanguage: AppLanguage = .system
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        if let release = updateManager.visibleAvailableRelease {
            Section {
                Text(appLanguage.localized("menu.update_available.title", release.displayVersion))
                Button(appLanguage.localized("action.download_update")) {
                    updateManager.openReleasePage(release)
                }
                Button(appLanguage.localized("action.skip_version")) {
                    updateManager.skipRelease(release)
                }
            }
            Divider()
        }

        if !permissionManager.isAccessibilityGranted {
            Section {
                Text(appLanguage.localized("menu.accessibility_missing.title"))
                Text(appLanguage.localized("menu.accessibility_missing.subtitle"))
                    .font(.caption)
                Button(appLanguage.localized("action.open_settings_now")) {
                    permissionManager.openAccessibilitySettings()
                }
            }
            Divider()
        }

        if !inputSourceManager.isEnglish {
            Section {
                Text(appLanguage.localized("menu.input_source_warning.title", inputSourceManager.currentSourceName))
                Text(appLanguage.localized("menu.input_source_warning.subtitle"))
                    .font(.caption)
                Button(appLanguage.localized("action.switch_to_us")) {
                    _ = inputSourceManager.switchToEnglishInputSource()
                }
            }
            Divider()
        }

        // Inline menu items to switch between Telex, VNI, and VIQR
        Picker(appLanguage.localized("menu.input_style"), selection: $selectedStyle) {
            ForEach(InputMethodStyle.allCases) { style in
                Text(style.title).tag(style)
            }
        }
        .pickerStyle(.inline)
        .onChange(of: selectedStyle) { _, newValue in
            KeyTapManager.shared.inputMethodStyle = newValue
        }

        Divider()

        Toggle(appLanguage.localized("menu.launch_at_login"), isOn: launchAtLoginManager.binding)

        Picker(appLanguage.localized("menu.language"), selection: $appLanguage) {
            ForEach(AppLanguage.allCases) { option in
                Text(option.displayName(in: appLanguage)).tag(option)
            }
        }
        .pickerStyle(.menu)

        Divider()

        Button(appLanguage.localized("menu.settings")) {
            openWindow(id: "settings")
            NSApplication.shared.activate(ignoringOtherApps: true)
        }
        .keyboardShortcut(",")

        Divider()

        Button(appLanguage.localized("menu.quit")) {
            NSApplication.shared.terminate(nil)
        }
        .keyboardShortcut("q")
    }
}

@main struct OKeyTreMain: App {
    private static let relaunchPidFlag = "--relaunched-from-pid"

    @AppStorage(AppLanguage.defaultsKey) private var appLanguage: AppLanguage = .system

    init() {
        let env = ProcessInfo.processInfo.environment
        let isPreview = env["XCODE_RUNNING_FOR_PREVIEWS"] == "1"
        let isRunningTests = env["XCTestConfigurationFilePath"] != nil
            || env["XCTestBundlePath"] != nil
            || env["XCTestSessionIdentifier"] != nil

        if !isPreview && !isRunningTests {
            Self.terminateExistingInstances()
            Self.relaunchViaLaunchServicesIfNeeded(env: env)
        }

        UserDefaults.standard.register(defaults: [
            InputMethodBuffer.modernToneDefaultsKey: InputMethodBuffer.defaultModernTone,
            UpdateManager.autoCheckDefaultsKey: UpdateManager.defaultAutoCheck,
        ])

        NSApplication.shared.setActivationPolicy(.accessory)
        _ = KeyTapManager.shared

        if !isPreview && !isRunningTests {
            Self.terminateExistingInstances()
            LaunchAtLoginManager.shared.enableByDefaultIfNeeded()
            PermissionManager.shared.requestAccessibility()
            InputSourceManager.shared.checkAndWarnOnStartup()
            UpdateManager.shared.startAutomaticChecksIfNeeded()
        }
    }

    /// When executed directly from a shell (`./OKeyTre.app/Contents/MacOS/OKeyTre`), the parent PID is
    /// the shell rather than `launchd` (`1`), causing macOS TCC to attribute Accessibility checks
    /// to Terminal. Re-launching the `.app` bundle via `NSWorkspace` hands responsibility to the
    /// app bundle itself before any Accessibility API is touched.
    private static func relaunchViaLaunchServicesIfNeeded(env: [String: String]) {
        let bundleURL = Bundle.main.bundleURL
        guard getppid() != 1,
              bundleURL.pathExtension == "app",
              !CommandLine.arguments.contains(relaunchPidFlag),
              env["__XCODE_BUILT_PRODUCTS_DIR_PATHS"] == nil,
              !isDebuggerAttached() else {
            return
        }

        let launchURL = stageEphemeralBundleIfNeeded(bundleURL)
        let config = NSWorkspace.OpenConfiguration()
        config.createsNewApplicationInstance = true
        config.arguments = [relaunchPidFlag, "\(getpid())"]

        let handoffFailed = OSAllocatedUnfairLock(initialState: false)
        NSWorkspace.shared.openApplication(at: launchURL, configuration: config) { _, error in
            if error == nil {
                Darwin._exit(0)
            }
            handoffFailed.withLock { $0 = true }
        }

        let deadline = Date(timeIntervalSinceNow: 2.0)
        while !handoffFailed.withLock({ $0 }) && Date() < deadline {
            _ = RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.02))
        }
    }

    /// `bazel run` unpacks the `.app` archive into a temporary `bazel_temp.XXXXXX` directory and
    /// registers a shell `EXIT` trap (`rm -rf`) that deletes the directory as soon as the direct
    /// child exits. Cloning the bundle to a persistent cache folder before `_exit(0)` keeps
    /// `Contents/Resources/*.lproj` and `AppIcon.icns` intact for the LaunchServices-spawned copy.
    private static func stageEphemeralBundleIfNeeded(_ bundleURL: URL) -> URL {
        let parentName = bundleURL.deletingLastPathComponent().lastPathComponent
        guard parentName.hasPrefix("bazel_temp.") else {
            return bundleURL
        }

        let fm = FileManager.default
        guard let cachesDir = fm.urls(for: .cachesDirectory, in: .userDomainMask).first else {
            return bundleURL
        }
        let stageDir = cachesDir.appendingPathComponent("com.okeytre.OKeyTre.cli-stage", isDirectory: true)
        let stagedAppURL = stageDir.appendingPathComponent(bundleURL.lastPathComponent, isDirectory: true)

        do {
            try fm.createDirectory(at: stageDir, withIntermediateDirectories: true)
            if fm.fileExists(atPath: stagedAppURL.path) {
                try fm.removeItem(at: stagedAppURL)
            }
            try fm.copyItem(at: bundleURL, to: stagedAppURL)
            try? fm.setAttributes([.modificationDate: Date()], ofItemAtPath: stagedAppURL.path)
            return stagedAppURL
        } catch {
            return bundleURL
        }
    }

    private static func isDebuggerAttached() -> Bool {
        var info = kinfo_proc()
        var size = MemoryLayout<kinfo_proc>.stride
        var mib: [Int32] = [CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()]
        let status = sysctl(&mib, UInt32(mib.count), &info, &size, nil, 0)
        guard status == 0 else { return false }
        return (info.kp_proc.p_flag & P_TRACED) != 0
    }

    private static func relaunchedParentPid() -> pid_t? {
        let args = CommandLine.arguments
        guard let idx = args.firstIndex(of: relaunchPidFlag),
              idx + 1 < args.count,
              let pid = Int32(args[idx + 1]) else {
            return nil
        }
        return pid
    }

    private static func isMatchingAppInstance(
        _ app: NSRunningApplication,
        currentBundleId: String?
    ) -> Bool {
        if let id = app.bundleIdentifier {
            if let currentBundleId, id == currentBundleId {
                return true
            }
            return id == "com.okeytre.OKeyTre" || id.hasSuffix(".OKeyTre")
        }
        return app.executableURL?.lastPathComponent == "OKeyTre"
    }

    private static func terminateExistingInstances() {
        let currentPid = ProcessInfo.processInfo.processIdentifier
        let handoffPid = relaunchedParentPid()
        let currentBundleId = Bundle.main.bundleIdentifier
        let otherInstances = NSWorkspace.shared.runningApplications.filter { app in
            let pid = app.processIdentifier
            return pid > 0
                && pid != currentPid
                && pid != handoffPid
                && !app.isTerminated
                && isMatchingAppInstance(app, currentBundleId: currentBundleId)
        }

        let pids = otherInstances.map(\.processIdentifier)
        guard !pids.isEmpty else { return }

        for app in otherInstances {
            _ = app.terminate()
            _ = kill(app.processIdentifier, SIGTERM)
        }

        for _ in 0..<40 {
            if !pids.contains(where: { kill($0, 0) == 0 }) {
                return
            }
            usleep(10_000)
        }

        for pid in pids where kill(pid, 0) == 0 {
            _ = kill(pid, SIGKILL)
        }

        for _ in 0..<20 {
            if !pids.contains(where: { kill($0, 0) == 0 }) {
                return
            }
            usleep(10_000)
        }
    }

    var body: some Scene {
        MenuBarExtra {
            MenuBarMenuContent()
        } label: {
            MenuBarStatusView()
        }

        Window(appLanguage.localized("settings.window_title"), id: "settings") {
            ContentView()
                .environment(\.locale, appLanguage.locale)
        }
        .windowResizability(.contentSize)
    }
}
