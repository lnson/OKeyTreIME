// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import AppKit
import SwiftUI

enum SettingsSection: String, CaseIterable, Identifiable {
    case general
    case correction
    case advanced

    var id: String { rawValue }

    func title(in language: AppLanguage) -> String {
        switch self {
        case .general:
            return language.localized("settings.section.general")
        case .correction:
            return language.localized("settings.section.correction")
        case .advanced:
            return language.localized("settings.section.advanced")
        }
    }

    var systemImage: String {
        switch self {
        case .general:
            return "gearshape.fill"
        case .correction:
            return "wand.and.stars"
        case .advanced:
            return "gearshape.2.fill"
        }
    }

    var tintColor: Color {
        switch self {
        case .general:
            return .teal
        case .correction:
            return .blue
        case .advanced:
            return .gray
        }
    }
}

struct ContentView: View {
    @AppStorage(AppLanguage.defaultsKey) private var appLanguage: AppLanguage = .system
    @State private var selectedSection: SettingsSection? = .general

    var body: some View {
        NavigationSplitView {
            List(SettingsSection.allCases, selection: $selectedSection) { section in
                Label {
                    Text(section.title(in: appLanguage))
                        .font(.body)
                } icon: {
                    Image(systemName: section.systemImage)
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(.white)
                        .frame(width: 22, height: 22)
                        .background(
                            RoundedRectangle(cornerRadius: 5, style: .continuous)
                                .fill(section.tintColor.gradient)
                        )
                }
                .tag(section)
                .padding(.vertical, 2)
            }
            .listStyle(.sidebar)
            .safeAreaInset(edge: .bottom) {
                SidebarFooter(appLanguage: $appLanguage)
            }
            .navigationSplitViewColumnWidth(min: 195, ideal: 215, max: 240)
        } detail: {
            switch selectedSection ?? .general {
            case .general:
                GeneralSettingsPane()
            case .correction:
                CorrectionSettingsPane()
            case .advanced:
                AdvancedSettingsPane()
            }
        }
        .frame(minWidth: 640, idealWidth: 680, minHeight: 420, idealHeight: 460)
    }
}

private struct SidebarFooter: View {
    @Binding var appLanguage: AppLanguage

    private static let logoImage: NSImage =
        NSImage(named: "AppIcon") ?? NSApplication.shared.applicationIconImage

    var body: some View {
        VStack(spacing: 12) {
            HStack(spacing: 6) {
                Image(systemName: "globe")
                    .font(.subheadline)
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

            VStack(spacing: 6) {
                Image(nsImage: Self.logoImage)
                    .resizable()
                    .interpolation(.high)
                    .aspectRatio(contentMode: .fit)
                    .frame(width: 68, height: 68)
                    .clipShape(RoundedRectangle(cornerRadius: 15, style: .continuous))
                    .shadow(color: .black.opacity(0.16), radius: 4, y: 2)

                VStack(spacing: 1) {
                    Text(appLanguage.localized("app.name"))
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(.primary)

                    Text(appLanguage.localized("settings.sidebar.version", UpdateManager.currentAppVersionString))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
            }
            .allowsHitTesting(false)
        }
        .frame(maxWidth: .infinity)
        .padding(.horizontal, 12)
        .padding(.vertical, 14)
    }
}

private struct GeneralSettingsPane: View {
    @AppStorage(AppLanguage.defaultsKey) private var appLanguage: AppLanguage = .system
    @ObservedObject private var keyTapManager = KeyTapManager.shared
    @ObservedObject private var launchAtLoginManager = LaunchAtLoginManager.shared
    @ObservedObject private var updateManager = UpdateManager.shared

    var body: some View {
        Form {
            Section {
                Picker(selection: keyTapManager.styleBinding) {
                    ForEach(InputMethodStyle.allCases) { style in
                        Text(style.displayName(in: appLanguage)).tag(style)
                    }
                } label: {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(appLanguage.localized("menu.input_style"))
                            .font(.body)
                        Text(appLanguage.localized("settings.general.input_style.description"))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                }

                Toggle(isOn: launchAtLoginManager.binding) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(appLanguage.localized("menu.launch_at_login"))
                            .font(.body)
                        Text(appLanguage.localized("settings.general.launch_at_login.description"))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                }
            } header: {
                Text(appLanguage.localized("settings.general.system.header"))
            }

            Section {
                Toggle(isOn: updateManager.autoCheckBinding) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(appLanguage.localized("settings.updates.auto_check.title"))
                            .font(.body)
                        Text(appLanguage.localized("settings.updates.auto_check.description"))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                }

                LabeledContent {
                    HStack(spacing: 8) {
                        if let release = updateManager.availableRelease {
                            Button(appLanguage.localized("action.download_update")) {
                                updateManager.openReleasePage(release)
                            }
                            .buttonStyle(.borderedProminent)
                        }

                        Button(appLanguage.localized("settings.sidebar.check_updates")) {
                            updateManager.checkForUpdatesNow()
                        }
                        .disabled(updateManager.isChecking)
                    }
                } label: {
                    VStack(alignment: .leading, spacing: 3) {
                        if let release = updateManager.availableRelease {
                            Text(appLanguage.localized("settings.updates.available_status", release.displayVersion))
                                .font(.body.weight(.medium))
                        } else {
                            Text(appLanguage.localized("settings.updates.current_version", UpdateManager.currentAppVersionString))
                                .font(.body)
                        }

                        if updateManager.isChecking {
                            Text(appLanguage.localized("settings.sidebar.checking_updates"))
                                .font(.subheadline)
                                .foregroundStyle(.secondary)
                        } else if updateManager.didConfirmUpToDate {
                            Text(appLanguage.localized("settings.sidebar.up_to_date"))
                                .font(.subheadline)
                                .foregroundStyle(.secondary)
                        }
                    }
                }

                LabeledContent {
                    Link(destination: UpdateManager.releasesWebURL) {
                        HStack(spacing: 4) {
                            Text(appLanguage.localized("settings.updates.manual_download.link"))
                            Image(systemName: "arrow.up.right.square")
                                .font(.caption)
                        }
                    }
                } label: {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(appLanguage.localized("settings.updates.manual_download.title"))
                            .font(.body)
                        Text(appLanguage.localized("settings.updates.manual_download.description"))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                }
            } header: {
                Text(appLanguage.localized("settings.updates.header"))
            }
        }
        .formStyle(.grouped)
        .navigationTitle(appLanguage.localized("settings.section.general"))
        .onAppear {
            launchAtLoginManager.refreshStatus()
        }
    }
}

private struct CorrectionSettingsPane: View {
    @AppStorage(AppLanguage.defaultsKey) private var appLanguage: AppLanguage = .system
    @AppStorage(InputMethodBuffer.modernToneDefaultsKey) private var modernTone = InputMethodBuffer.defaultModernTone
    @AppStorage(InputMethodBuffer.restoreInvalidWordsDefaultsKey)
    private var restoreInvalidWords = InputMethodBuffer.defaultRestoreInvalidWords
    @AppStorage(InputMethodBuffer.uoHornDefaultsKey) private var uoHorn = false
    @AppStorage(InputMethodBuffer.ngToNghDefaultsKey) private var ngToNgh = false
    @AppStorage(InputMethodBuffer.nghToNgDefaultsKey) private var nghToNg = false

    var body: some View {
        Form {
            Section {
                Toggle(isOn: $modernTone) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(appLanguage.localized("settings.orthography.modern_tone.title"))
                            .font(.body)
                        Text(appLanguage.localized("settings.orthography.modern_tone.description"))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                }
                .onChange(of: modernTone) { _, newValue in
                    KeyTapManager.shared.modernTone = newValue
                }
            } header: {
                Text(appLanguage.localized("settings.orthography.header"))
            }

            Section {
                Toggle(isOn: $restoreInvalidWords) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(appLanguage.localized("settings.english.restore_invalid.title"))
                            .font(.body)
                        Text(appLanguage.localized("settings.english.restore_invalid.description"))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                }
                .onChange(of: restoreInvalidWords) { _, newValue in
                    KeyTapManager.shared.restoreInvalidWords = newValue
                }
            } header: {
                Text(appLanguage.localized("settings.english.header"))
            }

            Section {
                Toggle(isOn: $uoHorn) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(appLanguage.localized("settings.correction.uo_horn.title"))
                            .font(.body)
                        Text(appLanguage.localized("settings.correction.uo_horn.description"))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                }
                .onChange(of: uoHorn) { _, newValue in
                    KeyTapManager.shared.uoHorn = newValue
                }

                Toggle(isOn: $ngToNgh) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(appLanguage.localized("settings.correction.ng_to_ngh.title"))
                            .font(.body)
                        Text(appLanguage.localized("settings.correction.ng_to_ngh.description"))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                }
                .onChange(of: ngToNgh) { _, newValue in
                    KeyTapManager.shared.ngToNgh = newValue
                }

                Toggle(isOn: $nghToNg) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(appLanguage.localized("settings.correction.ngh_to_ng.title"))
                            .font(.body)
                        Text(appLanguage.localized("settings.correction.ngh_to_ng.description"))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                }
                .onChange(of: nghToNg) { _, newValue in
                    KeyTapManager.shared.nghToNg = newValue
                }
            } header: {
                Text(appLanguage.localized("settings.correction.header"))
            }
        }
        .formStyle(.grouped)
        .navigationTitle(appLanguage.localized("settings.section.correction"))
    }
}

private struct AdvancedSettingsPane: View {
    @AppStorage(AppLanguage.defaultsKey) private var appLanguage: AppLanguage = .system
    @AppStorage(InputMethodBuffer.maxBufferSizeDefaultsKey)
    private var maxBufferSize: Int = InputMethodBuffer.defaultMaxBufferSize

    private static let numberFormatter: NumberFormatter = {
        let formatter = NumberFormatter()
        formatter.numberStyle = .none
        formatter.minimum = NSNumber(value: InputMethodBuffer.minBufferSize)
        formatter.maximum = NSNumber(value: InputMethodBuffer.maxAllowedBufferSize)
        return formatter
    }()

    var body: some View {
        Form {
            Section {
                LabeledContent {
                    HStack(alignment: .center, spacing: 6) {
                        TextField(
                            "",
                            value: $maxBufferSize,
                            formatter: Self.numberFormatter
                        )
                        .labelsHidden()
                        .textFieldStyle(.roundedBorder)
                        .multilineTextAlignment(.trailing)
                        .monospacedDigit()
                        .frame(width: 56)

                        Stepper(
                            "",
                            value: $maxBufferSize,
                            in: InputMethodBuffer.minBufferSize...InputMethodBuffer.maxAllowedBufferSize,
                            step: 8
                        )
                        .labelsHidden()
                        .fixedSize()
                    }
                    .alignmentGuide(.firstTextBaseline) { d in
                        d[VerticalAlignment.center]
                    }
                } label: {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(appLanguage.localized("settings.advanced.buffer_size.title"))
                            .font(.body)
                        Text(appLanguage.localized("settings.advanced.buffer_size.description"))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                }
                .onChange(of: maxBufferSize) { _, newValue in
                    let clamped = min(
                        max(newValue, InputMethodBuffer.minBufferSize),
                        InputMethodBuffer.maxAllowedBufferSize
                    )
                    if clamped != newValue {
                        maxBufferSize = clamped
                    }
                    KeyTapManager.shared.maxBufferSize = clamped
                }
            } header: {
                Text(appLanguage.localized("settings.advanced.header"))
            }
        }
        .formStyle(.grouped)
        .navigationTitle(appLanguage.localized("settings.section.advanced"))
    }
}

#Preview {
    ContentView()
}
