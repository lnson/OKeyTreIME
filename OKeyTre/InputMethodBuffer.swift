// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Thin Swift bridge around the stateful C++ `okeytre::InputMethodBuffer`
// (`OKTInputMethodBuffer`), wiring macOS `CaretTextReader` Accessibility
// context seeding into the C++ keystroke session.

import Foundation
import CoreGraphics

/// Supported Vietnamese input method styles (plus `.off` to pass keystrokes through untouched).
enum InputMethodStyle: String, CaseIterable, Identifiable {
    case off, telex, vni, viqr

    var id: String { rawValue }

    var isVietnameseEnabled: Bool {
        self != .off
    }

    func displayName(in language: AppLanguage) -> String {
        switch self {
        case .telex:
            return "Telex"
        case .vni:
            return "VNI"
        case .viqr:
            return "VIQR"
        case .off:
            return language.localized("menu.input_style.off")
        }
    }

    fileprivate var cStyle: OKTInputMethodStyle {
        switch self {
        case .telex, .off:
            return OKT_STYLE_TELEX
        case .vni:
            return OKT_STYLE_VNI
        case .viqr:
            return OKT_STYLE_VIQR
        }
    }
}

final class InputMethodBuffer {
    struct Action: Equatable {
        let backspaceCount: Int
        let text: String
    }

    /// UserDefaults key controlling the selected input method style (`InputMethodStyle.rawValue`).
    static let inputMethodDefaultsKey = "inputMethodStyle"
    /// UserDefaults key remembering the last active non-`.off` style for `Left ⌘ + Right ⌘` toggling.
    static let lastActiveStyleDefaultsKey = "lastActiveInputMethodStyle"
    /// UserDefaults key controlling modern Vietnamese tone placement (`oà, oé, uý` vs `òa, óe, úy`).
    static let modernToneDefaultsKey = "modernTone"
    /// Default setting for modern Vietnamese tone placement (`oà, oé, uý`).
    static let defaultModernTone = true
    /// UserDefaults key controlling the `ư + o -> ươ` (`?uoHorn`) rule.
    static let uoHornDefaultsKey = "uoHorn"
    /// UserDefaults key controlling the `ng + [ie] -> ngh[ie]` (`?ngToNgh`) rule.
    static let ngToNghDefaultsKey = "ngToNgh"
    /// UserDefaults key controlling the `ngh + [aou] -> ng[aou]` (`?nghToNg`) rule.
    static let nghToNgDefaultsKey = "nghToNg"
    /// UserDefaults key controlling automatic raw-word restoration on word end for invalid Vietnamese syllables.
    static let restoreInvalidWordsDefaultsKey = "restoreInvalidWords"
    /// Default setting for automatic raw-word restoration on word end.
    static let defaultRestoreInvalidWords = true
    /// UserDefaults key controlling the maximum number of characters retained in the buffer before the caret.
    static let maxBufferSizeDefaultsKey = "maxBufferSize"
    /// Default maximum number of characters retained in the buffer across word boundaries.
    static let defaultMaxBufferSize = Int(OKT_BUFFER_DEFAULT_MAX_SIZE)
    /// Minimum allowed buffer size (enough for a single Vietnamese word).
    static let minBufferSize = Int(OKT_BUFFER_MIN_SIZE)
    /// Maximum allowed buffer size.
    static let maxAllowedBufferSize = Int(OKT_BUFFER_MAX_SIZE)

    private let rawBuffer: OpaquePointer

    var style: InputMethodStyle {
        didSet {
            if style == .off {
                reset()
            } else {
                okeytre_buffer_set_style(rawBuffer, style.cStyle)
            }
        }
    }

    deinit {
        okeytre_buffer_destroy(rawBuffer)
    }

    var maxBufferSize: Int {
        get { Int(okeytre_buffer_get_max_size(rawBuffer)) }
        set { okeytre_buffer_set_max_size(rawBuffer, Int32(newValue)) }
    }

    var modernTone: Bool {
        get { okeytre_buffer_get_modern_tone(rawBuffer) }
        set { okeytre_buffer_set_modern_tone(rawBuffer, newValue) }
    }

    var uoHorn: Bool {
        get { okeytre_buffer_get_uo_horn(rawBuffer) }
        set { okeytre_buffer_set_uo_horn(rawBuffer, newValue) }
    }

    var ngToNgh: Bool {
        get { okeytre_buffer_get_ng_to_ngh(rawBuffer) }
        set { okeytre_buffer_set_ng_to_ngh(rawBuffer, newValue) }
    }

    var nghToNg: Bool {
        get { okeytre_buffer_get_ngh_to_ng(rawBuffer) }
        set { okeytre_buffer_set_ngh_to_ng(rawBuffer, newValue) }
    }

    var restoreInvalidWords: Bool {
        get { okeytre_buffer_get_restore_invalid_words(rawBuffer) }
        set { okeytre_buffer_set_restore_invalid_words(rawBuffer, newValue) }
    }

    static func resolvedModernTone(from defaults: UserDefaults = .standard) -> Bool {
        defaults.object(forKey: modernToneDefaultsKey) as? Bool ?? defaultModernTone
    }

    static func resolvedRestoreInvalidWords(from defaults: UserDefaults = .standard) -> Bool {
        defaults.object(forKey: restoreInvalidWordsDefaultsKey) as? Bool ?? defaultRestoreInvalidWords
    }

    init(
        style: InputMethodStyle = .telex,
        modernTone: Bool = defaultModernTone,
        uoHorn: Bool = false,
        ngToNgh: Bool = false,
        nghToNg: Bool = false,
        restoreInvalidWords: Bool = defaultRestoreInvalidWords,
        maxBufferSize: Int = defaultMaxBufferSize
    ) {
        self.rawBuffer = okeytre_buffer_create(
            style.cStyle,
            modernTone,
            uoHorn,
            ngToNgh,
            nghToNg,
            Int32(maxBufferSize)
        )!
        okeytre_buffer_set_restore_invalid_words(self.rawBuffer, restoreInvalidWords)
        self.style = style
    }

    func reset() {
        okeytre_buffer_reset(rawBuffer)
    }

    private static func toOKTModifiers(_ flags: CGEventFlags) -> UInt32 {
        var mask: UInt32 = 0
        if flags.contains(.maskShift) {
            mask |= UInt32(OKT_MOD_SHIFT.rawValue)
        }
        if flags.contains(.maskControl) {
            mask |= UInt32(OKT_MOD_CONTROL.rawValue)
        }
        if flags.contains(.maskAlternate) {
            mask |= UInt32(OKT_MOD_OPTION.rawValue)
        }
        if flags.contains(.maskCommand) {
            mask |= UInt32(OKT_MOD_COMMAND.rawValue)
        }
        return mask
    }

    private static func decodeAction(
        _ body: (UnsafeMutableBufferPointer<UniChar>, inout Int32, inout Int32) -> Bool
    ) -> Action? {
        var backspaceCount: Int32 = 0
        var outLen: Int32 = 0
        return withUnsafeTemporaryAllocation(of: UniChar.self, capacity: 1024) { outBuf in
            guard body(outBuf, &backspaceCount, &outLen) else { return nil }
            let text = outLen > 0
                ? String(utf16CodeUnits: outBuf.baseAddress!, count: Int(outLen))
                : ""
            return Action(backspaceCount: Int(backspaceCount), text: text)
        }
    }

    func seedWithSurroundingText(_ word: [UniChar], caretOffset: Int) {
        word.withUnsafeBufferPointer { ptr in
            okeytre_buffer_seed_with_surrounding_text(
                rawBuffer,
                ptr.baseAddress,
                Int32(ptr.count),
                Int32(caretOffset)
            )
        }
    }

    private func seedCaretIfNeeded() {
        guard okeytre_buffer_consume_caret_read_request(rawBuffer) else { return }
        guard let surrounding = CaretTextReader.getSurroundingText() else { return }
        seedWithSurroundingText(surrounding.wordUTF16, caretOffset: surrounding.caretOffset)
    }

    @discardableResult
    func restoreCurrentWord() -> Action? {
        guard style.isVietnameseEnabled else { return nil }
        return Self.decodeAction { outBuf, backspaceCount, outLen in
            okeytre_buffer_restore_current_word(
                rawBuffer,
                &backspaceCount,
                outBuf.baseAddress,
                Int32(outBuf.count),
                &outLen
            )
        }
    }

    @discardableResult
    func handleInput(_ input: UnsafeBufferPointer<UniChar>, flags: CGEventFlags = []) -> Action? {
        guard style.isVietnameseEnabled else { return nil }
        if flags.intersection(.disruptingModifiers).isEmpty {
            seedCaretIfNeeded()
        }

        return Self.decodeAction { outBuf, backspaceCount, outLen in
            okeytre_buffer_handle_input(
                rawBuffer,
                input.baseAddress,
                Int32(input.count),
                Self.toOKTModifiers(flags),
                &backspaceCount,
                outBuf.baseAddress,
                Int32(outBuf.count),
                &outLen
            )
        }
    }
}
