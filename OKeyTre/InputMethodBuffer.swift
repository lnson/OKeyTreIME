// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Thin Swift bridge around the stateful C++ `okeytre::InputMethodBuffer`
// (`OKTInputMethodBuffer`), wiring macOS `CaretTextReader` Accessibility
// context seeding into the C++ keystroke session.

import Foundation
import CoreGraphics

/// Supported Vietnamese input method styles.
enum InputMethodStyle: String, CaseIterable, Identifiable {
    case telex = "telex"
    case vni = "vni"
    case viqr = "viqr"

    var id: String { rawValue }

    var title: String {
        switch self {
        case .telex:
            return "Telex"
        case .vni:
            return "VNI"
        case .viqr:
            return "VIQR"
        }
    }

    fileprivate var cStyle: OKTInputMethodStyle {
        switch self {
        case .telex:
            return OKT_STYLE_TELEX
        case .vni:
            return OKT_STYLE_VNI
        case .viqr:
            return OKT_STYLE_VIQR
        }
    }

    fileprivate init(cStyle: OKTInputMethodStyle) {
        switch cStyle {
        case OKT_STYLE_VNI:
            self = .vni
        case OKT_STYLE_VIQR:
            self = .viqr
        default:
            self = .telex
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
    /// UserDefaults key controlling modern Vietnamese tone placement (`oà, oé, uý` vs `òa, óe, úy`).
    static let modernToneDefaultsKey = "modernTone"
    /// UserDefaults key controlling the `ư + o -> ươ` (`?uoHorn`) rule.
    static let uoHornDefaultsKey = "uoHorn"
    /// UserDefaults key controlling the `ng + [ie] -> ngh[ie]` (`?ngToNgh`) rule.
    static let ngToNghDefaultsKey = "ngToNgh"
    /// UserDefaults key controlling the `ngh + [aou] -> ng[aou]` (`?nghToNg`) rule.
    static let nghToNgDefaultsKey = "nghToNg"
    /// UserDefaults key controlling the maximum number of characters retained in the buffer before the caret.
    static let maxBufferSizeDefaultsKey = "maxBufferSize"
    /// Default maximum number of characters retained in the buffer across word boundaries.
    static let defaultMaxBufferSize = Int(OKT_BUFFER_DEFAULT_MAX_SIZE)
    /// Minimum allowed buffer size (enough for a single Vietnamese word).
    static let minBufferSize = Int(OKT_BUFFER_MIN_SIZE)
    /// Maximum allowed buffer size.
    static let maxAllowedBufferSize = Int(OKT_BUFFER_MAX_SIZE)

    private let rawBuffer: OpaquePointer

    deinit {
        okeytre_buffer_destroy(rawBuffer)
    }

    var maxBufferSize: Int {
        get { Int(okeytre_buffer_get_max_size(rawBuffer)) }
        set { okeytre_buffer_set_max_size(rawBuffer, Int32(newValue)) }
    }

    var style: InputMethodStyle {
        InputMethodStyle(cStyle: okeytre_buffer_get_style(rawBuffer))
    }

    func setStyle(_ newStyle: InputMethodStyle) {
        okeytre_buffer_set_style(rawBuffer, newStyle.cStyle)
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

    init(
        style: InputMethodStyle = .telex,
        modernTone: Bool = false,
        uoHorn: Bool = false,
        ngToNgh: Bool = false,
        nghToNg: Bool = false,
        maxBufferSize: Int = InputMethodBuffer.defaultMaxBufferSize
    ) {
        self.rawBuffer = okeytre_buffer_create(
            style.cStyle,
            modernTone,
            uoHorn,
            ngToNgh,
            nghToNg,
            Int32(maxBufferSize)
        )!
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
        let buffer = rawBuffer
        return Self.decodeAction { outBuf, backspaceCount, outLen in
            okeytre_buffer_restore_current_word(
                buffer,
                &backspaceCount,
                outBuf.baseAddress,
                Int32(outBuf.count),
                &outLen
            )
        }
    }

    @discardableResult
    func handleInput(_ input: UnsafeBufferPointer<UniChar>, flags: CGEventFlags = []) -> Action? {
        let disruptingModifiers: CGEventFlags = [.maskCommand, .maskControl, .maskAlternate]
        if flags.intersection(disruptingModifiers).isEmpty {
            seedCaretIfNeeded()
        }

        let modifiers = Self.toOKTModifiers(flags)
        let buffer = rawBuffer
        return Self.decodeAction { outBuf, backspaceCount, outLen in
            okeytre_buffer_handle_input(
                buffer,
                input.baseAddress,
                Int32(input.count),
                modifiers,
                &backspaceCount,
                outBuf.baseAddress,
                Int32(outBuf.count),
                &outLen
            )
        }
    }
}
