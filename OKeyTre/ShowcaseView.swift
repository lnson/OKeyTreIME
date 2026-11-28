// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import AppKit
import Combine
import SwiftUI

final class ShowcaseManager: ObservableObject {
    enum PresentationMode: Equatable {
        case firstInstall
        case featureUpdate(version: String)
    }

    static let shared = ShowcaseManager()

    /// Bump this version string whenever a release introduces new user-facing functionality that
    /// should re-open the Showcase window once for upgrading users. Leave unchanged on pure
    /// bug-fix releases so users are only notified when new capabilities ship.
    nonisolated static let currentFeatureMilestoneVersionString = "0.3"
    static let currentFeatureMilestoneVersion = AppVersion(currentFeatureMilestoneVersionString)!

    nonisolated static let lastSeenShowcaseVersionDefaultsKey = "lastSeenShowcaseVersion"
    nonisolated static let legacyHasSeenShowcaseDefaultsKey = "hasSeenFeatureShowcase"

    @Published private(set) var presentationMode: PresentationMode = .firstInstall

    private var window: NSWindow?
    private let defaults: UserDefaults
    private let milestoneVersionString: String
    private let milestoneVersion: AppVersion
    private let presentWindowHandler: (PresentationMode) -> Void

    init(
        defaults: UserDefaults = .standard,
        milestoneVersionString: String = ShowcaseManager.currentFeatureMilestoneVersionString,
        presentWindowHandler: ((PresentationMode) -> Void)? = nil
    ) {
        self.defaults = defaults
        self.milestoneVersionString = milestoneVersionString
        self.milestoneVersion = AppVersion(milestoneVersionString)
            ?? Self.currentFeatureMilestoneVersion
        self.presentWindowHandler = presentWindowHandler ?? { _ in }
    }

    func resolveLaunchPresentation(defaults: UserDefaults? = nil) -> PresentationMode? {
        let store = defaults ?? self.defaults
        if let seenRaw = store.string(forKey: Self.lastSeenShowcaseVersionDefaultsKey),
           let seenVersion = AppVersion(seenRaw) {
            guard seenVersion < milestoneVersion else {
                return nil
            }
            return .featureUpdate(version: milestoneVersionString)
        }

        let hasPriorInstallState = store.bool(forKey: Self.legacyHasSeenShowcaseDefaultsKey)
            || store.object(forKey: LaunchAtLoginManager.hasConfiguredDefaultsKey) != nil
            || store.object(forKey: PermissionManager.lastRegisteredCDHashDefaultsKey) != nil
            || store.object(forKey: InputMethodBuffer.inputMethodDefaultsKey) != nil

        if hasPriorInstallState {
            return .featureUpdate(version: milestoneVersionString)
        }
        return .firstInstall
    }

    func shouldPresentOnLaunch(defaults: UserDefaults? = nil) -> Bool {
        resolveLaunchPresentation(defaults: defaults) != nil
    }

    func markShowcaseSeen(defaults: UserDefaults? = nil) {
        let store = defaults ?? self.defaults
        store.set(milestoneVersionString, forKey: Self.lastSeenShowcaseVersionDefaultsKey)
        store.set(true, forKey: Self.legacyHasSeenShowcaseDefaultsKey)
    }

    @discardableResult
    func presentOnLaunchIfNeeded(defaults: UserDefaults? = nil) -> Bool {
        let store = defaults ?? self.defaults
        guard let mode = resolveLaunchPresentation(defaults: store) else {
            return false
        }
        presentationMode = mode
        markShowcaseSeen(defaults: store)
        if self === Self.shared {
            showWindow(mode: mode)
        } else {
            presentWindowHandler(mode)
        }
        return true
    }

    func showWindow(mode: PresentationMode = .firstInstall) {
        presentationMode = mode
        markShowcaseSeen()
        let targetWindow = ensureWindow()
        targetWindow.title = windowTitle(for: mode, language: AppLanguage.current)
        DockIconCoordinator.shared.showShowcaseWindow {
            if !targetWindow.isVisible {
                targetWindow.center()
            }
            targetWindow.makeKeyAndOrderFront(nil)
        }
    }

    func closeWindow() {
        window?.close()
    }

    private func windowTitle(for mode: PresentationMode, language: AppLanguage) -> String {
        switch mode {
        case .firstInstall:
            return language.localized("showcase.window_title")
        case let .featureUpdate(version):
            return language.localized("showcase.whats_new.title", version)
        }
    }

    private func ensureWindow() -> NSWindow {
        if let window {
            return window
        }

        let contentView = ShowcaseView(showcaseManager: self) { [weak self] in
            self?.closeWindow()
        }
        let hostingController = NSHostingController(rootView: contentView)

        let created = NSWindow(contentViewController: hostingController)
        created.title = windowTitle(for: presentationMode, language: AppLanguage.current)
        created.styleMask = [.titled, .closable, .miniaturizable, .fullSizeContentView]
        created.titlebarAppearsTransparent = true
        created.titleVisibility = .hidden
        created.isMovableByWindowBackground = true
        created.isReleasedWhenClosed = false
        created.isRestorable = false
        created.setContentSize(NSSize(width: 620, height: 545))
        created.center()

        DockIconCoordinator.shared.registerShowcaseWindow(created)
        self.window = created
        return created
    }
}

struct ShowcaseView: View {
    @ObservedObject var showcaseManager: ShowcaseManager
    var onDismiss: () -> Void

    @ObservedObject private var permissionManager = PermissionManager.shared
    @ObservedObject private var inputSourceManager = InputSourceManager.shared
    @ObservedObject private var keyTapManager = KeyTapManager.shared
    @AppStorage(AppLanguage.defaultsKey) private var appLanguage: AppLanguage = .system
    @State private var practiceText: String = ""

    private static let logoImage: NSImage =
        NSImage(named: "AppIcon") ?? NSApplication.shared.applicationIconImage

    private static let selectableStyles: [InputMethodStyle] = [.telex, .vni, .viqr]

    private var isFeatureUpdate: Bool {
        if case .featureUpdate = showcaseManager.presentationMode {
            return true
        }
        return false
    }

    private var headerTitle: String {
        switch showcaseManager.presentationMode {
        case .firstInstall:
            return appLanguage.localized("showcase.title")
        case let .featureUpdate(version):
            return appLanguage.localized("showcase.whats_new.title", version)
        }
    }

    var body: some View {
        VStack(spacing: 18) {
            topLanguageBar

            heroHeader

            setupStatusBanner

            showcaseGrid

            practiceSandbox

            footerBar
        }
        .padding(.horizontal, 26)
        .padding(.top, 10)
        .padding(.bottom, 22)
        .frame(width: 620)
        .environment(\.locale, appLanguage.locale)
    }

    private var topLanguageBar: some View {
        HStack {
            Spacer()
            HStack(spacing: 5) {
                Image(systemName: "globe")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                Picker(appLanguage.localized("menu.language"), selection: $appLanguage) {
                    ForEach(AppLanguage.allCases) { option in
                        Text(option.displayName(in: appLanguage)).tag(option)
                    }
                }
                .labelsHidden()
                .pickerStyle(.menu)
                .controlSize(.small)
                .fixedSize()
            }
        }
    }

    private var heroHeader: some View {
        VStack(spacing: 8) {
            Image(nsImage: Self.logoImage)
                .resizable()
                .interpolation(.high)
                .aspectRatio(contentMode: .fit)
                .frame(width: 64, height: 64)
                .clipShape(RoundedRectangle(cornerRadius: 15, style: .continuous))
                .shadow(color: .black.opacity(0.16), radius: 5, y: 2)

            Text(headerTitle)
                .font(.title2.weight(.bold))

            Text(appLanguage.localized("showcase.subtitle"))
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    @ViewBuilder
    private var setupStatusBanner: some View {
        if !permissionManager.isAccessibilityGranted {
            warningBanner(
                icon: "exclamationmark.shield.fill",
                title: appLanguage.localized("showcase.status.accessibility_title"),
                subtitle: appLanguage.localized("showcase.status.accessibility_desc"),
                actionTitle: appLanguage.localized("action.open_settings_now")
            ) {
                permissionManager.openAccessibilitySettings()
            }
        } else if keyTapManager.isVietnameseEnabled && !inputSourceManager.isEnglish {
            warningBanner(
                icon: "keyboard.badge.exclamationmark",
                title: appLanguage.localized("menu.input_source_warning.title", inputSourceManager.currentSourceName),
                subtitle: appLanguage.localized("menu.input_source_warning.subtitle"),
                actionTitle: appLanguage.localized("action.switch_to_us")
            ) {
                _ = inputSourceManager.switchToEnglishInputSource()
            }
        } else {
            HStack(spacing: 8) {
                Image(systemName: "checkmark.circle.fill")
                    .foregroundStyle(.green)
                Text(appLanguage.localized("showcase.status.ready"))
                    .font(.subheadline.weight(.medium))
                Spacer()
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            .background(
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .fill(Color.green.opacity(0.10))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .strokeBorder(Color.green.opacity(0.25), lineWidth: 1)
            )
        }
    }

    private func warningBanner(
        icon: String,
        title: String,
        subtitle: String,
        actionTitle: String,
        action: @escaping () -> Void
    ) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .font(.title3)
                .foregroundStyle(.orange)

            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(.subheadline.weight(.semibold))
                Text(subtitle)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Spacer(minLength: 8)

            Button(actionTitle, action: action)
                .buttonStyle(.borderedProminent)
                .tint(.orange)
                .controlSize(.small)
        }
        .padding(12)
        .background(
            RoundedRectangle(cornerRadius: 11, style: .continuous)
                .fill(Color.orange.opacity(0.10))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 11, style: .continuous)
                .strokeBorder(Color.orange.opacity(0.28), lineWidth: 1)
        )
    }

    private var showcaseGrid: some View {
        let columns = [
            GridItem(.flexible(), spacing: 12),
            GridItem(.flexible(), spacing: 12),
        ]
        let newBadge = isFeatureUpdate ? appLanguage.localized("showcase.badge.new") : nil

        return LazyVGrid(columns: columns, spacing: 12) {
            ShowcaseDemoCard(
                title: appLanguage.localized("showcase.card.auto_restore.title"),
                badge: newBadge,
                detail: appLanguage.localized("showcase.card.auto_restore.desc")
            ) {
                HStack(spacing: 5) {
                    ShowcaseTypedPill(text: "úer")
                    Text("+").foregroundStyle(.secondary)
                    ShowcaseKeycap(text: "Space")
                    Text("→").foregroundStyle(.secondary)
                    ShowcaseResultPill(text: "user␣")
                }
            }

            ShowcaseDemoCard(
                title: appLanguage.localized("showcase.card.dual_shift.title"),
                badge: newBadge,
                detail: appLanguage.localized("showcase.card.dual_shift.desc")
            ) {
                HStack(spacing: 5) {
                    ShowcaseTypedPill(text: "rồ␣")
                    Text("+").foregroundStyle(.secondary)
                    ShowcaseKeycap(text: "⇧ Left")
                    ShowcaseKeycap(text: "⇧ Right")
                    Text("→").foregroundStyle(.secondary)
                    ShowcaseResultPill(text: "roof␣")
                }
            }

            ShowcaseDemoCard(
                title: appLanguage.localized("showcase.card.diacritics.title"),
                badge: nil,
                detail: appLanguage.localized("showcase.card.diacritics.desc")
            ) {
                HStack(spacing: 5) {
                    ShowcaseTypedPill(text: "tieng")
                    Text("+").foregroundStyle(.secondary)
                    ShowcaseKeycap(text: "e")
                    ShowcaseKeycap(text: "s")
                    Text("→").foregroundStyle(.secondary)
                    ShowcaseResultPill(text: "tiếng")
                }
            }

            ShowcaseDemoCard(
                title: appLanguage.localized("showcase.card.toggle.title"),
                badge: nil,
                detail: appLanguage.localized("showcase.card.toggle.desc")
            ) {
                HStack(spacing: 5) {
                    ShowcaseKeycap(text: "⌘ Left")
                    Text("+").foregroundStyle(.secondary)
                    ShowcaseKeycap(text: "⌘ Right")
                    Text("→").foregroundStyle(.secondary)
                    ShowcaseResultPill(text: appLanguage.localized("showcase.card.toggle.result"))
                }
            }
        }
    }

    private var practiceSandbox: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Label(appLanguage.localized("showcase.practice.label"), systemImage: "character.cursor.ibeam")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)

                Spacer()

                Text(keyTapManager.inputMethodStyle.displayName(in: appLanguage))
                    .font(.caption2.weight(.semibold))
                    .padding(.horizontal, 7)
                    .padding(.vertical, 2)
                    .background(
                        Capsule()
                            .fill(keyTapManager.isVietnameseEnabled ? Color.accentColor.opacity(0.14) : Color.secondary.opacity(0.14))
                    )
                    .foregroundStyle(keyTapManager.isVietnameseEnabled ? Color.accentColor : .secondary)
            }

            TextField(
                appLanguage.localized("showcase.practice.placeholder"),
                text: $practiceText
            )
            .textFieldStyle(.roundedBorder)
            .controlSize(.large)
        }
    }

    private var footerBar: some View {
        HStack(spacing: 12) {
            HStack(spacing: 8) {
                Text(appLanguage.localized("menu.input_style"))
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                Picker(
                    appLanguage.localized("menu.input_style"),
                    selection: Binding(
                        get: {
                            keyTapManager.inputMethodStyle == .off ? .telex : keyTapManager.inputMethodStyle
                        },
                        set: { newStyle in
                            keyTapManager.setInputMethodStyle(newStyle)
                        }
                    )
                ) {
                    ForEach(Self.selectableStyles) { style in
                        Text(style.displayName(in: appLanguage)).tag(style)
                    }
                }
                .labelsHidden()
                .pickerStyle(.segmented)
                .fixedSize()
            }

            Spacer()

            Button(appLanguage.localized("showcase.action.get_started")) {
                onDismiss()
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .keyboardShortcut(.defaultAction)
        }
        .padding(.top, 2)
    }
}

private struct ShowcaseDemoCard<DemoContent: View>: View {
    let title: String
    let badge: String?
    let detail: String
    @ViewBuilder let demo: () -> DemoContent

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 6) {
                Text(title)
                    .font(.caption.weight(.bold))
                    .foregroundStyle(.teal)
                    .lineLimit(1)

                if let badge {
                    Text(badge)
                        .font(.system(size: 9, weight: .bold))
                        .padding(.horizontal, 5)
                        .padding(.vertical, 1.5)
                        .background(Capsule().fill(Color.teal))
                        .foregroundStyle(.white)
                }
            }

            demo()
                .font(.system(size: 12, weight: .medium, design: .monospaced))

            Text(detail)
                .font(.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(12)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Color(nsColor: .controlBackgroundColor))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(Color.primary.opacity(0.09), lineWidth: 1)
        )
    }
}

private struct ShowcaseTypedPill: View {
    let text: String

    var body: some View {
        Text(text)
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(
                RoundedRectangle(cornerRadius: 5, style: .continuous)
                    .fill(Color.primary.opacity(0.05))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 5, style: .continuous)
                    .strokeBorder(style: StrokeStyle(lineWidth: 1, dash: [3, 2]))
                    .foregroundStyle(Color.primary.opacity(0.22))
            )
    }
}

private struct ShowcaseKeycap: View {
    let text: String

    var body: some View {
        Text(text)
            .font(.system(size: 11, weight: .medium))
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(
                RoundedRectangle(cornerRadius: 5, style: .continuous)
                    .fill(Color(nsColor: .windowBackgroundColor))
                    .shadow(color: .black.opacity(0.10), radius: 0.5, y: 1)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 5, style: .continuous)
                    .strokeBorder(Color.primary.opacity(0.20), lineWidth: 1)
            )
    }
}

private struct ShowcaseResultPill: View {
    let text: String

    var body: some View {
        Text(text)
            .fontWeight(.bold)
            .foregroundStyle(.teal)
            .padding(.horizontal, 7)
            .padding(.vertical, 2)
            .background(
                RoundedRectangle(cornerRadius: 5, style: .continuous)
                    .fill(Color.teal.opacity(0.14))
            )
    }
}
