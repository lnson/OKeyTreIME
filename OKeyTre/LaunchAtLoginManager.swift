// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import AppKit
import Combine
import ServiceManagement
import SwiftUI

final class LaunchAtLoginManager: ObservableObject {
    static let shared = LaunchAtLoginManager()

    @Published private(set) var isEnabled: Bool = (SMAppService.mainApp.status == .enabled)

    private init() {
        NotificationCenter.default.addObserver(
            forName: NSApplication.didBecomeActiveNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.refreshStatus()
        }
    }

    var binding: Binding<Bool> {
        Binding(
            get: { self.statusIsEnabled },
            set: { self.setEnabled($0) }
        )
    }

    private var statusIsEnabled: Bool {
        let current = (SMAppService.mainApp.status == .enabled)
        if isEnabled != current {
            DispatchQueue.main.async { [weak self] in
                self?.isEnabled = current
            }
        }
        return current
    }

    func refreshStatus() {
        let current = (SMAppService.mainApp.status == .enabled)
        if isEnabled != current {
            isEnabled = current
        }
    }

    func setEnabled(_ enabled: Bool) {
        refreshStatus()
        guard enabled != isEnabled else { return }
        do {
            if enabled {
                try SMAppService.mainApp.register()
                if SMAppService.mainApp.status == .requiresApproval {
                    SMAppService.openSystemSettingsLoginItems()
                }
            } else {
                try SMAppService.mainApp.unregister()
            }
        } catch {
            print("Failed to update launch-at-login state: \(error)")
        }
        refreshStatus()
    }
}
