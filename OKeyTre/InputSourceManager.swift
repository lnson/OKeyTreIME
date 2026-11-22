// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import Foundation
import Combine
import Carbon
import AppKit

final class InputSourceManager: ObservableObject {
    static let shared = InputSourceManager()
    
    @Published var isEnglish: Bool = true
    @Published var currentSourceName: String = "U.S."
    
    private init() {
        updateCurrentSource()
        observeSourceChanges()
    }
    
    func updateCurrentSource() {
        guard let source = TISCopyCurrentKeyboardInputSource()?.takeRetainedValue() else { return }
        
        var id = ""
        if let idPtr = TISGetInputSourceProperty(source, kTISPropertyInputSourceID) {
            id = Unmanaged<CFString>.fromOpaque(idPtr).takeUnretainedValue() as String
        }
        
        var name = AppLanguage.current.localized("input_source.unknown")
        if let namePtr = TISGetInputSourceProperty(source, kTISPropertyLocalizedName) {
            name = Unmanaged<CFString>.fromOpaque(namePtr).takeUnretainedValue() as String
        }
        
        let english = id.hasPrefix("com.apple.keylayout.US") ||
                      id.hasPrefix("com.apple.keylayout.ABC") ||
                      id.lowercased().contains("english")
        
        DispatchQueue.main.async {
            self.currentSourceName = name
            self.isEnglish = english
        }
    }
    
    private func observeSourceChanges() {
        // Use CFNotificationCenterGetDistributedCenter with deliverImmediately
        // to guarantee delivery across processes on macOS even when app is an accessory/background agent
        let observer = Unmanaged.passUnretained(self).toOpaque()
        CFNotificationCenterAddObserver(
            CFNotificationCenterGetDistributedCenter(),
            observer,
            { (_, observer, _, _, _) in
                guard let observer = observer else { return }
                let manager = Unmanaged<InputSourceManager>.fromOpaque(observer).takeUnretainedValue()
                manager.updateCurrentSource()
            },
            kTISNotifySelectedKeyboardInputSourceChanged,
            nil,
            .deliverImmediately
        )
    }
    
    func checkAndWarnOnStartup() {
        updateCurrentSource()
        guard !isEnglish else { return }
        
        let lang = AppLanguage.current
        let alert = NSAlert()
        alert.messageText = lang.localized("alert.non_english_keyboard.title")
        alert.informativeText = lang.localized("alert.non_english_keyboard.message", currentSourceName)
        alert.alertStyle = .warning
        alert.addButton(withTitle: lang.localized("action.switch_to_us"))
        alert.addButton(withTitle: lang.localized("alert.non_english_keyboard.open_keyboard_settings"))
        alert.addButton(withTitle: lang.localized("alert.non_english_keyboard.ignore"))
        
        let response = alert.runModal()
        if response == .alertFirstButtonReturn {
            _ = switchToEnglishInputSource()
        } else if response == .alertSecondButtonReturn {
            if let url = URL(string: "x-apple.systempreferences:com.apple.Keyboard-Settings.extension") {
                NSWorkspace.shared.open(url)
            }
        }
    }
    
    @discardableResult
    func switchToEnglishInputSource() -> Bool {
        guard let list = TISCreateInputSourceList(nil, false)?.takeRetainedValue() as? [TISInputSource] else {
            return false
        }
        for source in list {
            if let idPtr = TISGetInputSourceProperty(source, kTISPropertyInputSourceID) {
                let id = Unmanaged<CFString>.fromOpaque(idPtr).takeUnretainedValue() as String
                if id == "com.apple.keylayout.US" || id == "com.apple.keylayout.ABC" {
                    let status = TISSelectInputSource(source)
                    if status == noErr {
                        updateCurrentSource()
                        return true
                    }
                }
            }
        }
        return false
    }
}
