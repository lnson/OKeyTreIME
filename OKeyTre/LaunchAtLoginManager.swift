// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import AppKit
import Combine
import Foundation
import ServiceManagement
import SwiftUI

final class LaunchAtLoginManager: ObservableObject {
    struct ServiceBackend {
        var isEnabled: () -> Bool
        var requiresApproval: () -> Bool
        var register: () throws -> Void
        var unregister: () throws -> Void
        var openLoginItemsSettings: () -> Void

        static let live = ServiceBackend(
            isEnabled: { SMAppService.mainApp.status == .enabled },
            requiresApproval: { SMAppService.mainApp.status == .requiresApproval },
            register: { try SMAppService.mainApp.register() },
            unregister: { try SMAppService.mainApp.unregister() },
            openLoginItemsSettings: { SMAppService.openSystemSettingsLoginItems() }
        )
    }

    static let shared = LaunchAtLoginManager()
    nonisolated static let hasConfiguredDefaultsKey = "hasConfiguredLaunchAtLogin"

    @Published private(set) var isEnabled: Bool

    private let userDefaults: UserDefaults
    private let backend: ServiceBackend
    private var activeObserver: NSObjectProtocol?

    init(
        userDefaults: UserDefaults = .standard,
        backend: ServiceBackend = .live,
        observeAppActivation: Bool = true
    ) {
        self.userDefaults = userDefaults
        self.backend = backend
        self.isEnabled = backend.isEnabled()

        if observeAppActivation {
            activeObserver = NotificationCenter.default.addObserver(
                forName: NSApplication.didBecomeActiveNotification,
                object: nil,
                queue: .main
            ) { [weak self] _ in
                self?.refreshStatus()
            }
        }
    }

    deinit {
        if let activeObserver {
            NotificationCenter.default.removeObserver(activeObserver)
        }
    }

    var binding: Binding<Bool> {
        Binding(
            get: { self.statusIsEnabled },
            set: { self.setEnabled($0) }
        )
    }

    private var statusIsEnabled: Bool {
        let current = backend.isEnabled()
        if isEnabled != current {
            DispatchQueue.main.async { [weak self] in
                self?.isEnabled = current
            }
        }
        return current
    }

    func refreshStatus() {
        let current = backend.isEnabled()
        if isEnabled != current {
            isEnabled = current
        }
    }

    /// Registers Launch at Login once on initial launch if the user has not configured it yet.
    func enableByDefaultIfNeeded() {
        guard !userDefaults.bool(forKey: Self.hasConfiguredDefaultsKey) else {
            refreshStatus()
            return
        }

        if backend.isEnabled() {
            userDefaults.set(true, forKey: Self.hasConfiguredDefaultsKey)
            refreshStatus()
            return
        }

        do {
            try backend.register()
            userDefaults.set(true, forKey: Self.hasConfiguredDefaultsKey)
        } catch {
            print("Failed to enable default launch-at-login state: \(error)")
        }
        refreshStatus()
    }

    func setEnabled(_ enabled: Bool) {
        userDefaults.set(true, forKey: Self.hasConfiguredDefaultsKey)
        refreshStatus()
        guard enabled != isEnabled else { return }
        do {
            if enabled {
                try backend.register()
                if backend.requiresApproval() {
                    backend.openLoginItemsSettings()
                }
            } else {
                try backend.unregister()
            }
        } catch {
            print("Failed to update launch-at-login state: \(error)")
        }
        refreshStatus()
    }
}
