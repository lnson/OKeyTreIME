// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import AppKit
import CoreText
import Darwin
import SwiftUI
import os

struct MenuBarStatusView: View {
    @ObservedObject var inputSourceManager = InputSourceManager.shared
    @ObservedObject var permissionManager = PermissionManager.shared
    @ObservedObject var keyTapManager = KeyTapManager.shared
    @AppStorage(AppLanguage.defaultsKey) private var appLanguage: AppLanguage = .system

    static func tooltipText(
        style: InputMethodStyle,
        language: AppLanguage,
        isAccessibilityGranted: Bool = true,
        isEnglishKeyboard: Bool = true,
        keyboardName: String = "U.S."
    ) -> String {
        let base = language.localized("menu.tooltip.mode", style.displayName(in: language))
        if !isAccessibilityGranted {
            return "\(base) · \(language.localized("menu.accessibility_missing.title"))"
        }
        if style.isVietnameseEnabled && !isEnglishKeyboard {
            return "\(base) · \(language.localized("menu.input_source_warning.title", keyboardName))"
        }
        return base
    }

    private static func findStatusBarButton(in view: NSView) -> NSStatusBarButton? {
        if let button = view as? NSStatusBarButton {
            return button
        }
        for subview in view.subviews {
            if let found = findStatusBarButton(in: subview) {
                return found
            }
        }
        return nil
    }

    static func refreshStatusBarTooltip() {
        let current = tooltipText(
            style: KeyTapManager.shared.inputMethodStyle,
            language: AppLanguage.current,
            isAccessibilityGranted: PermissionManager.shared.isAccessibilityGranted,
            isEnglishKeyboard: InputSourceManager.shared.isEnglish,
            keyboardName: InputSourceManager.shared.currentSourceName
        )
        applyStatusBarTooltip(current)
    }

    private static func applyStatusBarTooltip(_ text: String) {
        let update = { () -> Bool in
            for window in NSApplication.shared.windows {
                if let contentView = window.contentView,
                   let button = findStatusBarButton(in: contentView) {
                    if button.toolTip != text {
                        button.toolTip = text
                    }
                    return true
                }
            }
            return false
        }
        if !update() {
            for delay in [0.1, 0.4] {
                DispatchQueue.main.asyncAfter(deadline: .now() + delay) {
                    _ = update()
                }
            }
        }
    }

    /// Custom vector template icon ("Vi" + rounded I-beam cursor) matching the
    /// exact stroke weight, cap height, and baseline of SF Symbols' `character.cursor.ibeam`.
    private static let vietnameseCursorIcon: NSImage = {
        let height: CGFloat = 16.0
        let fontSize: CGFloat = 11.5
        let baseFont = NSFont.systemFont(ofSize: fontSize, weight: .medium)
        let font = baseFont.fontDescriptor.withDesign(.rounded)
            .flatMap { NSFont(descriptor: $0, size: fontSize) } ?? baseFont

        let attrs: [NSAttributedString.Key: Any] = [
            .font: font,
            .foregroundColor: NSColor.black,
            .kern: -0.35,
        ]
        let text = NSAttributedString(string: "Vi", attributes: attrs)
        let textSize = text.size()

        let strokeWidth: CGFloat = 1.1
        let barWidth: CGFloat = 3.8
        let padLeft: CGFloat = 0.5
        let gap: CGFloat = 1.4
        let padRight: CGFloat = 0.8

        let ibeamLeftX = padLeft + textSize.width + gap
        let ibeamCenterX = ibeamLeftX + barWidth / 2.0
        let width = ceil(ibeamLeftX + barWidth + padRight)

        let image = NSImage(size: NSSize(width: width, height: height), flipped: false) { _ in
            NSColor.black.setStroke()

            let bottomY: CGFloat = 1.75
            let topY: CGFloat = height - 1.75

            let path = NSBezierPath()
            path.lineWidth = strokeWidth
            path.lineCapStyle = .round

            path.move(to: NSPoint(x: ibeamCenterX, y: bottomY))
            path.line(to: NSPoint(x: ibeamCenterX, y: topY))

            path.move(to: NSPoint(x: ibeamCenterX - barWidth / 2.0, y: topY))
            path.line(to: NSPoint(x: ibeamCenterX + barWidth / 2.0, y: topY))

            path.move(to: NSPoint(x: ibeamCenterX - barWidth / 2.0, y: bottomY))
            path.line(to: NSPoint(x: ibeamCenterX + barWidth / 2.0, y: bottomY))

            path.stroke()

            let line = CTLineCreateWithAttributedString(text)
            if let ctx = NSGraphicsContext.current?.cgContext {
                let baselineY = (bottomY + topY - font.capHeight) / 2.0 - 0.5
                ctx.textPosition = CGPoint(x: padLeft, y: baselineY)
                CTLineDraw(line, ctx)
            }
            return true
        }
        image.isTemplate = true
        return image
    }()

    var body: some View {
        let systemIconName: String? = if !permissionManager.isAccessibilityGranted {
            "exclamationmark.triangle.fill"
        } else if !keyTapManager.isVietnameseEnabled {
            "nosign"
        } else if !inputSourceManager.isEnglish {
            "exclamationmark.triangle.fill"
        } else {
            nil
        }
        let tooltip = Self.tooltipText(
            style: keyTapManager.inputMethodStyle,
            language: appLanguage,
            isAccessibilityGranted: permissionManager.isAccessibilityGranted,
            isEnglishKeyboard: inputSourceManager.isEnglish,
            keyboardName: inputSourceManager.currentSourceName
        )
        let _ = DispatchQueue.main.async {
            Self.applyStatusBarTooltip(tooltip)
        }
        Group {
            if let systemIconName {
                Image(systemName: systemIconName)
            } else {
                Image(nsImage: Self.vietnameseCursorIcon)
            }
        }
        .id(systemIconName ?? "vi-cursor")
        .help(tooltip)
    }
}

private struct MenuBarMenuContent: View {
    @ObservedObject var inputSourceManager = InputSourceManager.shared
    @ObservedObject var permissionManager = PermissionManager.shared
    @ObservedObject var launchAtLoginManager = LaunchAtLoginManager.shared
    @ObservedObject var updateManager = UpdateManager.shared
    @ObservedObject var keyTapManager = KeyTapManager.shared
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

        if keyTapManager.isVietnameseEnabled && !inputSourceManager.isEnglish {
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

        // Inline menu items to switch between Telex, VNI, VIQR, and Off
        Picker(appLanguage.localized("menu.input_style"), selection: keyTapManager.styleBinding) {
            ForEach(InputMethodStyle.allCases) { style in
                Text(style.displayName(in: appLanguage)).tag(style)
            }
        }
        .pickerStyle(.inline)

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
    static let registerAccessibilityFlag = "--register-accessibility"

    @AppStorage(AppLanguage.defaultsKey) private var appLanguage: AppLanguage = .system

    init() {
        if CommandLine.arguments.contains(Self.registerAccessibilityFlag) {
            PermissionManager.runAccessibilityRegistrationHelperAndExit()
        }

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
            if KeyTapManager.shared.isVietnameseEnabled {
                InputSourceManager.shared.checkAndWarnOnStartup()
            }
            UpdateManager.shared.startAutomaticChecksIfNeeded()
            MenuBarStatusView.refreshStatusBarTooltip()
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
