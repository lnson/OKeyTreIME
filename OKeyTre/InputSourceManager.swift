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

    private var lastCheckedSource: TISInputSource?
    private var cocoaSelectionObserver: NSObjectProtocol?

    private init() {
        updateCurrentSource()
        observeSourceChanges()
    }

    deinit {
        if let cocoaSelectionObserver {
            NotificationCenter.default.removeObserver(cocoaSelectionObserver)
        }
    }

    /// Returns `true` only when the active macOS text input source is a plain English keyboard
    /// layout (such as U.S. or ABC) rather than a system input method / IME (such as macOS's
    /// built-in Vietnamese Telex/VNI/VIQR, Japanese, Chinese, Korean, or third-party IMEs) or
    /// a non-English keyboard layout.
    nonisolated static func isCompatibleEnglishKeyboard(
        sourceID: String,
        sourceType: String? = nil
    ) -> Bool {
        if let sourceType, sourceType != (kTISTypeKeyboardLayout as String) {
            return false
        }
        let lower = sourceID.lowercased()
        return sourceID.hasPrefix("com.apple.keylayout.US")
            || sourceID.hasPrefix("com.apple.keylayout.ABC")
            || sourceID == "com.apple.keylayout.British"
            || sourceID == "com.apple.keylayout.British-PC"
            || sourceID == "com.apple.keylayout.Australian"
            || sourceID == "com.apple.keylayout.Canadian"
            || lower.contains("english")
    }

    /// Fast path called on the main thread from `KeyTapManager` so switching to a system IME
    /// (via `⌃Space`, Globe/Fn, Caps Lock, or menu bar) takes effect before the very first
    /// keystroke is processed. `TISCopyCurrentKeyboardInputSource()` returns Carbon's cached
    /// per-process `TISInputSourceRef`, so unchanged checks are a single `CFEqual` comparison.
    @discardableResult
    func refreshIfCurrentSourceChanged() -> Bool {
        guard let source = TISCopyCurrentKeyboardInputSource()?.takeRetainedValue() else {
            return isEnglish
        }
        if Thread.isMainThread,
           let lastCheckedSource,
           CFEqual(source, lastCheckedSource) {
            return isEnglish
        }
        return applySource(source)
    }

    func updateCurrentSource() {
        guard let source = TISCopyCurrentKeyboardInputSource()?.takeRetainedValue() else { return }
        _ = applySource(source)
    }

    @discardableResult
    private func applySource(_ source: TISInputSource) -> Bool {
        var id = ""
        if let idPtr = TISGetInputSourceProperty(source, kTISPropertyInputSourceID) {
            id = Unmanaged<CFString>.fromOpaque(idPtr).takeUnretainedValue() as String
        }

        var sourceType: String?
        if let typePtr = TISGetInputSourceProperty(source, kTISPropertyInputSourceType) {
            sourceType = Unmanaged<CFString>.fromOpaque(typePtr).takeUnretainedValue() as String
        }

        var name = AppLanguage.current.localized("input_source.unknown")
        if let namePtr = TISGetInputSourceProperty(source, kTISPropertyLocalizedName) {
            name = Unmanaged<CFString>.fromOpaque(namePtr).takeUnretainedValue() as String
        }

        let english = Self.isCompatibleEnglishKeyboard(sourceID: id, sourceType: sourceType)

        if Thread.isMainThread {
            lastCheckedSource = source
            if currentSourceName != name {
                currentSourceName = name
            }
            if isEnglish != english {
                isEnglish = english
            }
        } else {
            DispatchQueue.main.async {
                self.lastCheckedSource = source
                if self.currentSourceName != name {
                    self.currentSourceName = name
                }
                if self.isEnglish != english {
                    self.isEnglish = english
                }
            }
        }
        return english
    }

    private func observeSourceChanges() {
        // Use CFNotificationCenterGetDistributedCenter with deliverImmediately
        // to guarantee delivery across processes on macOS even when app is an accessory/background agent.
        // Also schedule an async pass on the main queue in case Carbon HIToolbox updates its
        // per-process cache after our observer runs during the same notification turn.
        let observer = Unmanaged.passUnretained(self).toOpaque()
        CFNotificationCenterAddObserver(
            CFNotificationCenterGetDistributedCenter(),
            observer,
            { (_, observer, _, _, _) in
                guard let observer = observer else { return }
                let manager = Unmanaged<InputSourceManager>.fromOpaque(observer).takeUnretainedValue()
                manager.updateCurrentSource()
                DispatchQueue.main.async {
                    manager.updateCurrentSource()
                }
            },
            kTISNotifySelectedKeyboardInputSourceChanged,
            nil,
            .deliverImmediately
        )

        cocoaSelectionObserver = NotificationCenter.default.addObserver(
            forName: NSTextInputContext.keyboardSelectionDidChangeNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.updateCurrentSource()
        }
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
