// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import Testing
import Foundation
import CoreGraphics
@testable import OKeyTre

private let leftArrowKey: UniChar = 0xF702  // NSLeftArrowFunctionKey
private let backspaceKey: UniChar = 0x007F  // ASCII DEL

private extension InputMethodBuffer {
    convenience init(
        characters: [UniChar],
        pos: Int? = nil,
        style: InputMethodStyle = .telex,
        modernTone: Bool = false,
        uoHorn: Bool = false,
        ngToNgh: Bool = false,
        nghToNg: Bool = false,
        restoreInvalidWords: Bool = false,
        maxBufferSize: Int = InputMethodBuffer.defaultMaxBufferSize
    ) {
        self.init(
            style: style,
            modernTone: modernTone,
            uoHorn: uoHorn,
            ngToNgh: ngToNgh,
            nghToNg: nghToNg,
            restoreInvalidWords: restoreInvalidWords,
            maxBufferSize: maxBufferSize
        )
        seedWithSurroundingText(characters, caretOffset: pos ?? characters.count)
    }

    @discardableResult
    func handleInput(_ input: [UniChar], flags: CGEventFlags = []) -> Action? {
        input.withUnsafeBufferPointer { handleInput($0, flags: flags) }
    }

    static func processKey(
        existingText: String,
        caretPosition: Int? = nil,
        inputKey: UniChar,
        flags: CGEventFlags = [],
        style: InputMethodStyle = .telex,
        modernTone: Bool = false,
        uoHorn: Bool = false,
        ngToNgh: Bool = false,
        nghToNg: Bool = false
    ) -> String {
        let initialChars = Array(existingText.utf16)
        let caret = caretPosition ?? initialChars.count
        let ime = InputMethodBuffer(
            characters: initialChars,
            pos: caret,
            style: style,
            modernTone: modernTone,
            uoHorn: uoHorn,
            ngToNgh: ngToNgh,
            nghToNg: nghToNg
        )

        if let action = ime.handleInput([inputKey], flags: flags) {
            var textChars = initialChars
            let deleteStart = caret - action.backspaceCount
            textChars.removeSubrange(deleteStart..<caret)
            textChars.insert(contentsOf: action.text.utf16, at: deleteStart)
            return String(utf16CodeUnits: textChars, count: textChars.count)
        } else {
            var textChars = initialChars
            textChars.insert(inputKey, at: caret)
            return String(utf16CodeUnits: textChars, count: textChars.count)
        }
    }
}

struct OKeyTreTests {

    struct KeyInputTestCase: CustomTestStringConvertible {
        let existingText: String
        let inputKey: UniChar
        let caretPosition: Int?
        let modernTone: Bool
        let uoHorn: Bool
        let ngToNgh: Bool
        let nghToNg: Bool
        let expectedOutput: String
        let description: String

        var testDescription: String {
            description
        }

        init(
            existingText: String,
            inputKey: UniChar,
            caretPosition: Int? = nil,
            modernTone: Bool = false,
            uoHorn: Bool = false,
            ngToNgh: Bool = false,
            nghToNg: Bool = false,
            expectedOutput: String
        ) {
            self.existingText = existingText
            self.inputKey = inputKey
            self.caretPosition = caretPosition
            self.modernTone = modernTone
            self.uoHorn = uoHorn
            self.ngToNgh = ngToNgh
            self.nghToNg = nghToNg
            self.expectedOutput = expectedOutput

            // Build visual representation with '|' marking the caret position
            let utf16Chars = Array(existingText.utf16)
            let caret = caretPosition ?? utf16Chars.count
            var visualTextChars = utf16Chars
            visualTextChars.insert(0x007C /* '|' */, at: max(0, min(caret, visualTextChars.count)))
            let textWithCaret = String(utf16CodeUnits: visualTextChars, count: visualTextChars.count)

            let inputChar = String(utf16CodeUnits: [inputKey], count: 1)
            var flags: [String] = []
            if modernTone { flags.append("?modernTone") }
            if uoHorn { flags.append("?uoHorn") }
            if ngToNgh { flags.append("?ngToNgh") }
            if nghToNg { flags.append("?nghToNg") }
            let flagPrefix = flags.isEmpty ? "" : "[\(flags.joined(separator: ", "))] "
            self.description = "\(flagPrefix)\"\(textWithCaret)\" + '\(inputChar)' -> \"\(expectedOutput)\""
        }
    }

    @MainActor
    @Test("Assert output text for (existing text, input key, caret position) tuples", arguments: [
        // 1. Basic tone placement & traditional vs modern ("òa, óe, úy" vs "oà, oé, uý") rules
        KeyInputTestCase(existingText: "co", inputKey: 0x0073 /* 's' */, caretPosition: 2, expectedOutput: "có"),
        KeyInputTestCase(existingText: "meo", inputKey: 0x0066 /* 'f' */, caretPosition: 3, expectedOutput: "mèo"),
        KeyInputTestCase(existingText: "bao", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "báo"),
        KeyInputTestCase(existingText: "hoa", inputKey: 0x0066 /* 'f' */, caretPosition: 3, modernTone: false, expectedOutput: "hòa"),
        KeyInputTestCase(existingText: "khoe", inputKey: 0x0072 /* 'r' */, caretPosition: 4, modernTone: false, expectedOutput: "khỏe"),
        KeyInputTestCase(existingText: "thuy", inputKey: 0x0073 /* 's' */, caretPosition: 4, modernTone: false, expectedOutput: "thúy"),
        KeyInputTestCase(existingText: "hoa", inputKey: 0x0066 /* 'f' */, caretPosition: 3, modernTone: true, expectedOutput: "hoà"),
        KeyInputTestCase(existingText: "khoe", inputKey: 0x0072 /* 'r' */, caretPosition: 4, modernTone: true, expectedOutput: "khoẻ"),
        KeyInputTestCase(existingText: "thuy", inputKey: 0x0073 /* 's' */, caretPosition: 4, modernTone: true, expectedOutput: "thuý"),
        KeyInputTestCase(existingText: "hòa", inputKey: 0x006E /* 'n' */, caretPosition: 3, modernTone: false, expectedOutput: "hoàn"),
        KeyInputTestCase(existingText: "khóe", inputKey: 0x0074 /* 't' */, caretPosition: 4, modernTone: false, expectedOutput: "khoét"),
        KeyInputTestCase(existingText: "túy", inputKey: 0x0074 /* 't' */, caretPosition: 3, modernTone: false, expectedOutput: "tuýt"),

        // 2. Open huo vs huong
        KeyInputTestCase(existingText: "huo", inputKey: 0x0077 /* 'w' */, caretPosition: 3, expectedOutput: "huơ"),
        KeyInputTestCase(existingText: "huong", inputKey: 0x0077 /* 'w' */, caretPosition: 5, expectedOutput: "hương"),
        KeyInputTestCase(existingText: "huoc", inputKey: 0x0077 /* 'w' */, caretPosition: 4, expectedOutput: "hươc"),

        // 3. Qu onset rules
        KeyInputTestCase(existingText: "quo", inputKey: 0x0077 /* 'w' */, caretPosition: 3, expectedOutput: "quơ"),

        // 4. Thuở and huơ followed by space or consonant
        KeyInputTestCase(existingText: "thuở", inputKey: 0x0020 /* ' ' */, caretPosition: 4, expectedOutput: "thuở "),
        KeyInputTestCase(existingText: "huơ", inputKey: 0x0020 /* ' ' */, caretPosition: 3, expectedOutput: "huơ "),
        KeyInputTestCase(existingText: "huơ", inputKey: 0x006E /* 'n' */, caretPosition: 3, expectedOutput: "hươn"),

        // 5. Uppercase & Case Preservation Tests
        // Lowercase modified, uppercase modifying key
        KeyInputTestCase(existingText: "co", inputKey: 0x0053 /* 'S' */, caretPosition: 2, expectedOutput: "có"),
        KeyInputTestCase(existingText: "meo", inputKey: 0x0046 /* 'F' */, caretPosition: 3, expectedOutput: "mèo"),
        KeyInputTestCase(existingText: "a", inputKey: 0x0041 /* 'A' */, caretPosition: 1, expectedOutput: "â"),
        KeyInputTestCase(existingText: "d", inputKey: 0x0044 /* 'D' */, caretPosition: 1, expectedOutput: "đ"),
        KeyInputTestCase(existingText: "o", inputKey: 0x0057 /* 'W' */, caretPosition: 1, expectedOutput: "ơ"),

        // Uppercase modified, lowercase modifying key
        KeyInputTestCase(existingText: "CO", inputKey: 0x0073 /* 's' */, caretPosition: 2, expectedOutput: "CÓ"),
        KeyInputTestCase(existingText: "MEO", inputKey: 0x0066 /* 'f' */, caretPosition: 3, expectedOutput: "MÈO"),
        KeyInputTestCase(existingText: "A", inputKey: 0x0061 /* 'a' */, caretPosition: 1, expectedOutput: "Â"),
        KeyInputTestCase(existingText: "D", inputKey: 0x0064 /* 'd' */, caretPosition: 1, expectedOutput: "Đ"),
        KeyInputTestCase(existingText: "O", inputKey: 0x0077 /* 'w' */, caretPosition: 1, expectedOutput: "Ơ"),

        // Both uppercase
        KeyInputTestCase(existingText: "CO", inputKey: 0x0053 /* 'S' */, caretPosition: 2, expectedOutput: "CÓ"),
        KeyInputTestCase(existingText: "A", inputKey: 0x0041 /* 'A' */, caretPosition: 1, expectedOutput: "Â"),

        // Diphthong "UO" / "Uo" / "uo" with W / w
        KeyInputTestCase(existingText: "HUO", inputKey: 0x0077 /* 'w' */, caretPosition: 3, expectedOutput: "HUƠ"),
        KeyInputTestCase(existingText: "HUONG", inputKey: 0x0057 /* 'W' */, caretPosition: 5, expectedOutput: "HƯƠNG"),
        KeyInputTestCase(existingText: "Huong", inputKey: 0x0077 /* 'w' */, caretPosition: 5, expectedOutput: "Hương"),
        KeyInputTestCase(existingText: "QUO", inputKey: 0x0077 /* 'w' */, caretPosition: 3, expectedOutput: "QUƠ"),
        KeyInputTestCase(existingText: "Quo", inputKey: 0x0077 /* 'w' */, caretPosition: 3, expectedOutput: "Quơ"),

        // Undo retains modifying key's original case:
        // "â" + 'A' -> "aA" (the user's exact example)
        KeyInputTestCase(existingText: "â", inputKey: 0x0041 /* 'A' */, caretPosition: 1, expectedOutput: "aA"),
        // "â" + 'a' -> "aa"
        KeyInputTestCase(existingText: "â", inputKey: 0x0061 /* 'a' */, caretPosition: 1, expectedOutput: "aa"),
        // "Â" + 'A' -> "AA"
        KeyInputTestCase(existingText: "Â", inputKey: 0x0041 /* 'A' */, caretPosition: 1, expectedOutput: "AA"),
        // "Â" + 'a' -> "Aa"
        KeyInputTestCase(existingText: "Â", inputKey: 0x0061 /* 'a' */, caretPosition: 1, expectedOutput: "Aa"),
        // Tone undo
        KeyInputTestCase(existingText: "có", inputKey: 0x0053 /* 'S' */, caretPosition: 2, expectedOutput: "coS"),
        KeyInputTestCase(existingText: "CÓ", inputKey: 0x0073 /* 's' */, caretPosition: 2, expectedOutput: "COs"),
        // Diphthong undo
        KeyInputTestCase(existingText: "hương", inputKey: 0x0057 /* 'W' */, caretPosition: 5, expectedOutput: "huongW"),
        KeyInputTestCase(existingText: "HƯƠNG", inputKey: 0x0077 /* 'w' */, caretPosition: 5, expectedOutput: "HUONGw"),

        // Huơ + n with uppercase
        KeyInputTestCase(existingText: "HUƠ", inputKey: 0x006E /* 'n' */, caretPosition: 3, expectedOutput: "HƯƠn"),
        KeyInputTestCase(existingText: "HUƠ", inputKey: 0x004E /* 'N' */, caretPosition: 3, expectedOutput: "HƯƠN"),

        // 6. Delete tone marks with 'z' or 'Z'
        // Acute / Sắc: có -> co
        KeyInputTestCase(existingText: "có", inputKey: 0x007A /* 'z' */, caretPosition: 2, expectedOutput: "co"),
        KeyInputTestCase(existingText: "CÓ", inputKey: 0x005A /* 'Z' */, caretPosition: 2, expectedOutput: "CO"),
        KeyInputTestCase(existingText: "CÓ", inputKey: 0x007A /* 'z' */, caretPosition: 2, expectedOutput: "CO"),
        KeyInputTestCase(existingText: "có", inputKey: 0x005A /* 'Z' */, caretPosition: 2, expectedOutput: "co"),

        // Grave / Huyền: mèo -> meo
        KeyInputTestCase(existingText: "mèo", inputKey: 0x007A /* 'z' */, caretPosition: 3, expectedOutput: "meo"),
        KeyInputTestCase(existingText: "MÈO", inputKey: 0x007A /* 'z' */, caretPosition: 3, expectedOutput: "MEO"),

        // Hook / Hỏi: bả -> ba
        KeyInputTestCase(existingText: "bả", inputKey: 0x007A /* 'z' */, caretPosition: 2, expectedOutput: "ba"),
        KeyInputTestCase(existingText: "thuở", inputKey: 0x007A /* 'z' */, caretPosition: 4, expectedOutput: "thuơ"),

        // Tilde / Ngã: xã -> xa
        KeyInputTestCase(existingText: "xã", inputKey: 0x007A /* 'z' */, caretPosition: 2, expectedOutput: "xa"),

        // Dot / Nặng: bạn -> ban
        KeyInputTestCase(existingText: "bạn", inputKey: 0x007A /* 'z' */, caretPosition: 3, expectedOutput: "ban"),

        // Retains base vowel modifier (^, horn, breve):
        // ấ -> â, ế -> ê, ớ -> ơ, ứ -> ư, ắ -> ă
        KeyInputTestCase(existingText: "thế", inputKey: 0x007A /* 'z' */, caretPosition: 3, expectedOutput: "thê"),
        KeyInputTestCase(existingText: "THẾ", inputKey: 0x005A /* 'Z' */, caretPosition: 3, expectedOutput: "THÊ"),
        KeyInputTestCase(existingText: "mới", inputKey: 0x007A /* 'z' */, caretPosition: 3, expectedOutput: "mơi"),
        KeyInputTestCase(existingText: "nắng", inputKey: 0x007A /* 'z' */, caretPosition: 4, expectedOutput: "năng"),

        // When no tone mark is present, insert z or Z (matching typed case)
        KeyInputTestCase(existingText: "co", inputKey: 0x007A /* 'z' */, caretPosition: 2, expectedOutput: "coz"),
        KeyInputTestCase(existingText: "co", inputKey: 0x005A /* 'Z' */, caretPosition: 2, expectedOutput: "coZ"),
        KeyInputTestCase(existingText: "CO", inputKey: 0x007A /* 'z' */, caretPosition: 2, expectedOutput: "COz"),
        KeyInputTestCase(existingText: "CO", inputKey: 0x005A /* 'Z' */, caretPosition: 2, expectedOutput: "COZ"),
        KeyInputTestCase(existingText: "ba", inputKey: 0x007A /* 'z' */, caretPosition: 2, expectedOutput: "baz"),
        KeyInputTestCase(existingText: "ba", inputKey: 0x005A /* 'Z' */, caretPosition: 2, expectedOutput: "baZ"),
        KeyInputTestCase(existingText: "â", inputKey: 0x007A /* 'z' */, caretPosition: 1, expectedOutput: "âz"),
        KeyInputTestCase(existingText: "Â", inputKey: 0x005A /* 'Z' */, caretPosition: 1, expectedOutput: "ÂZ"),

        // 7. Block tone placement if a tone modifier key (f, s, r, x, j) appears after the vowel / in the coda
        KeyInputTestCase(existingText: "cos", inputKey: 0x0066 /* 'f' */, caretPosition: 3, expectedOutput: "cosf"),
        KeyInputTestCase(existingText: "cos", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "coss"),
        KeyInputTestCase(existingText: "COS", inputKey: 0x0046 /* 'F' */, caretPosition: 3, expectedOutput: "COSF"),
        KeyInputTestCase(existingText: "car", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "cars"),
        KeyInputTestCase(existingText: "box", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "boxs"),
        KeyInputTestCase(existingText: "if", inputKey: 0x0073 /* 's' */, caretPosition: 2, expectedOutput: "ifs"),
        KeyInputTestCase(existingText: "aj", inputKey: 0x0066 /* 'f' */, caretPosition: 2, expectedOutput: "ajf"),
        KeyInputTestCase(existingText: "cost", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "costs"),
        KeyInputTestCase(existingText: "start", inputKey: 0x0073 /* 's' */, caretPosition: 5, expectedOutput: "starts"),
        KeyInputTestCase(existingText: "first", inputKey: 0x0073 /* 's' */, caretPosition: 5, expectedOutput: "firsts"),
        // Valid Vietnamese onsets containing s, r, x still place tone marks normally
        KeyInputTestCase(existingText: "sa", inputKey: 0x0073 /* 's' */, caretPosition: 2, expectedOutput: "sá"),
        KeyInputTestCase(existingText: "san", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "sán"),
        KeyInputTestCase(existingText: "tra", inputKey: 0x0066 /* 'f' */, caretPosition: 3, expectedOutput: "trà"),
        KeyInputTestCase(existingText: "tranh", inputKey: 0x0073 /* 's' */, caretPosition: 5, expectedOutput: "tránh"),

        // 8. Block vowel modifiers (a, e, o, w) when a modifier key (f, s, j, r, x) appears after the target vowel
        KeyInputTestCase(existingText: "cas", inputKey: 0x0061 /* 'a' */, caretPosition: 3, expectedOutput: "casa"),
        KeyInputTestCase(existingText: "res", inputKey: 0x0065 /* 'e' */, caretPosition: 3, expectedOutput: "rese"),
        KeyInputTestCase(existingText: "tor", inputKey: 0x006F /* 'o' */, caretPosition: 3, expectedOutput: "toro"),
        KeyInputTestCase(existingText: "for", inputKey: 0x0077 /* 'w' */, caretPosition: 3, expectedOutput: "forw"),
        KeyInputTestCase(existingText: "soft", inputKey: 0x0077 /* 'w' */, caretPosition: 4, expectedOutput: "softw"),
        KeyInputTestCase(existingText: "pass", inputKey: 0x0077 /* 'w' */, caretPosition: 4, expectedOutput: "passw"),
        KeyInputTestCase(existingText: "quos", inputKey: 0x0077 /* 'w' */, caretPosition: 4, expectedOutput: "quosw"),
        KeyInputTestCase(existingText: "huos", inputKey: 0x0077 /* 'w' */, caretPosition: 4, expectedOutput: "huosw"),
        KeyInputTestCase(existingText: "escap", inputKey: 0x0065 /* 'e' */, caretPosition: 5, expectedOutput: "escape"),
        KeyInputTestCase(existingText: "SOFT", inputKey: 0x0057 /* 'W' */, caretPosition: 4, expectedOutput: "SOFTW"),
        // Valid Vietnamese onsets containing s, r, x still apply a, e, o, w modifiers normally
        KeyInputTestCase(existingText: "san", inputKey: 0x0061 /* 'a' */, caretPosition: 3, expectedOutput: "sân"),
        KeyInputTestCase(existingText: "sen", inputKey: 0x0065 /* 'e' */, caretPosition: 3, expectedOutput: "sên"),
        KeyInputTestCase(existingText: "song", inputKey: 0x006F /* 'o' */, caretPosition: 4, expectedOutput: "sông"),
        KeyInputTestCase(existingText: "suong", inputKey: 0x0077 /* 'w' */, caretPosition: 5, expectedOutput: "sương"),
        KeyInputTestCase(existingText: "truong", inputKey: 0x0077 /* 'w' */, caretPosition: 6, expectedOutput: "trương"),

        // 9. Block tone & vowel modifiers (f, s, r, x, j, a, e, o, w) when 'w' appears after a vowel
        KeyInputTestCase(existingText: "cow", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "cows"),
        KeyInputTestCase(existingText: "down", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "downs"),
        KeyInputTestCase(existingText: "law", inputKey: 0x0066 /* 'f' */, caretPosition: 3, expectedOutput: "lawf"),
        KeyInputTestCase(existingText: "raw", inputKey: 0x0061 /* 'a' */, caretPosition: 3, expectedOutput: "rawa"),
        KeyInputTestCase(existingText: "new", inputKey: 0x0065 /* 'e' */, caretPosition: 3, expectedOutput: "newe"),
        KeyInputTestCase(existingText: "low", inputKey: 0x006F /* 'o' */, caretPosition: 3, expectedOutput: "lowo"),
        KeyInputTestCase(existingText: "bow", inputKey: 0x0077 /* 'w' */, caretPosition: 3, expectedOutput: "boww"),
        KeyInputTestCase(existingText: "uow", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "uows"),
        KeyInputTestCase(existingText: "huongw", inputKey: 0x0073 /* 's' */, caretPosition: 6, expectedOutput: "huongws"),
        KeyInputTestCase(existingText: "DOWNS", inputKey: 0x004F /* 'O' */, caretPosition: 5, expectedOutput: "DOWNSO"),

        // 10. Require valid Vietnamese initial consonant onset (or empty onset) before modified vowel/diphthong
        // Valid multi-letter & single-letter onsets
        KeyInputTestCase(existingText: "pa", inputKey: 0x0073 /* 's' */, caretPosition: 2, expectedOutput: "pá"),
        KeyInputTestCase(existingText: "đa", inputKey: 0x0073 /* 's' */, caretPosition: 2, expectedOutput: "đá"),
        KeyInputTestCase(existingText: "cha", inputKey: 0x0066 /* 'f' */, caretPosition: 3, expectedOutput: "chà"),
        KeyInputTestCase(existingText: "ghe", inputKey: 0x0065 /* 'e' */, caretPosition: 3, expectedOutput: "ghê"),
        KeyInputTestCase(existingText: "gia", inputKey: 0x0066 /* 'f' */, caretPosition: 3, expectedOutput: "già"),
        KeyInputTestCase(existingText: "giau", inputKey: 0x0066 /* 'f' */, caretPosition: 4, expectedOutput: "giàu"),
        KeyInputTestCase(existingText: "giuong", inputKey: 0x0077 /* 'w' */, caretPosition: 6, expectedOutput: "giương"),
        KeyInputTestCase(existingText: "giương", inputKey: 0x0066 /* 'f' */, caretPosition: 6, expectedOutput: "giường"),
        KeyInputTestCase(existingText: "gi", inputKey: 0x0066 /* 'f' */, caretPosition: 2, expectedOutput: "gì"),
        KeyInputTestCase(existingText: "qua", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "quá"),
        KeyInputTestCase(existingText: "qui", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "quí"),
        KeyInputTestCase(existingText: "khuyen", inputKey: 0x0065 /* 'e' */, caretPosition: 6, expectedOutput: "khuyên"),
        KeyInputTestCase(existingText: "ngoan", inputKey: 0x0077 /* 'w' */, caretPosition: 5, expectedOutput: "ngoăn"),
        KeyInputTestCase(existingText: "nha", inputKey: 0x0066 /* 'f' */, caretPosition: 3, expectedOutput: "nhà"),
        KeyInputTestCase(existingText: "pho", inputKey: 0x0077 /* 'w' */, caretPosition: 3, expectedOutput: "phơ"),
        KeyInputTestCase(existingText: "nghieng", inputKey: 0x0065 /* 'e' */, caretPosition: 7, expectedOutput: "nghiêng"),
        KeyInputTestCase(existingText: "cau", inputKey: 0x0061 /* 'a' */, caretPosition: 3, expectedOutput: "câu"),
        KeyInputTestCase(existingText: "tuan", inputKey: 0x0061 /* 'a' */, caretPosition: 4, expectedOutput: "tuân"),
        KeyInputTestCase(existingText: "buon", inputKey: 0x006F /* 'o' */, caretPosition: 4, expectedOutput: "buôn"),

        // Disallowed onsets (f, j, w, z, bare q, English clusters, or multi-syllable prefixes) do not place marks
        KeyInputTestCase(existingText: "fa", inputKey: 0x0073 /* 's' */, caretPosition: 2, expectedOutput: "fas"),
        KeyInputTestCase(existingText: "ja", inputKey: 0x0073 /* 's' */, caretPosition: 2, expectedOutput: "jas"),
        KeyInputTestCase(existingText: "wa", inputKey: 0x0073 /* 's' */, caretPosition: 2, expectedOutput: "was"),
        KeyInputTestCase(existingText: "za", inputKey: 0x0073 /* 's' */, caretPosition: 2, expectedOutput: "zas"),
        KeyInputTestCase(existingText: "qu", inputKey: 0x0073 /* 's' */, caretPosition: 2, expectedOutput: "qus"),
        KeyInputTestCase(existingText: "wo", inputKey: 0x0077 /* 'w' */, caretPosition: 2, expectedOutput: "wow"),
        KeyInputTestCase(existingText: "fre", inputKey: 0x0065 /* 'e' */, caretPosition: 3, expectedOutput: "free"),
        KeyInputTestCase(existingText: "thre", inputKey: 0x0065 /* 'e' */, caretPosition: 4, expectedOutput: "three"),
        KeyInputTestCase(existingText: "flo", inputKey: 0x006F /* 'o' */, caretPosition: 3, expectedOutput: "floo"),
        KeyInputTestCase(existingText: "scho", inputKey: 0x006F /* 'o' */, caretPosition: 4, expectedOutput: "schoo"),
        KeyInputTestCase(existingText: "sta", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "stas"),
        KeyInputTestCase(existingText: "pla", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "plas"),
        KeyInputTestCase(existingText: "cla", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "clas"),
        KeyInputTestCase(existingText: "cafe", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "cafes"),
        KeyInputTestCase(existingText: "CAFE", inputKey: 0x0053 /* 'S' */, caretPosition: 4, expectedOutput: "CAFES"),
        KeyInputTestCase(existingText: "video", inputKey: 0x0066 /* 'f' */, caretPosition: 5, expectedOutput: "videof"),
        KeyInputTestCase(existingText: "aqua", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "aquas"),
        KeyInputTestCase(existingText: "logia", inputKey: 0x0066 /* 'f' */, caretPosition: 5, expectedOutput: "logiaf"),
        KeyInputTestCase(existingText: "okê", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "okês"),
        KeyInputTestCase(existingText: "camer", inputKey: 0x0061 /* 'a' */, caretPosition: 5, expectedOutput: "camera"),
        KeyInputTestCase(existingText: "aden", inputKey: 0x0061 /* 'a' */, caretPosition: 4, expectedOutput: "adena"),
        KeyInputTestCase(existingText: "are", inputKey: 0x0061 /* 'a' */, caretPosition: 3, expectedOutput: "area"),
        KeyInputTestCase(existingText: "websit", inputKey: 0x0065 /* 'e' */, caretPosition: 6, expectedOutput: "website"),
        KeyInputTestCase(existingText: "welcom", inputKey: 0x0065 /* 'e' */, caretPosition: 6, expectedOutput: "welcome"),
        KeyInputTestCase(existingText: "potat", inputKey: 0x006F /* 'o' */, caretPosition: 5, expectedOutput: "potato"),
        KeyInputTestCase(existingText: "token", inputKey: 0x006F /* 'o' */, caretPosition: 5, expectedOutput: "tokeno"),
        KeyInputTestCase(existingText: "unit", inputKey: 0x0077 /* 'w' */, caretPosition: 4, expectedOutput: "unitw"),
        KeyInputTestCase(existingText: "music", inputKey: 0x0077 /* 'w' */, caretPosition: 5, expectedOutput: "musicw"),
        KeyInputTestCase(existingText: "deci", inputKey: 0x0064 /* 'd' */, caretPosition: 4, expectedOutput: "decid"),
        KeyInputTestCase(existingText: "davi", inputKey: 0x0064 /* 'd' */, caretPosition: 4, expectedOutput: "david"),
        KeyInputTestCase(existingText: "ad", inputKey: 0x0064 /* 'd' */, caretPosition: 2, expectedOutput: "add"),

        // 11. Require valid Vietnamese final consonant coda (or empty coda): p, t, c, ch, m, n, ng, nh
        // Valid Vietnamese codas place marks
        KeyInputTestCase(existingText: "cap", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "cáp"),
        KeyInputTestCase(existingText: "cat", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "cát"),
        KeyInputTestCase(existingText: "cac", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "các"),
        KeyInputTestCase(existingText: "cach", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "cách"),
        KeyInputTestCase(existingText: "cam", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "cám"),
        KeyInputTestCase(existingText: "can", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "cán"),
        KeyInputTestCase(existingText: "cang", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "cáng"),
        KeyInputTestCase(existingText: "canh", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "cánh"),
        KeyInputTestCase(existingText: "tiêp", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "tiếp"),
        KeyInputTestCase(existingText: "viêt", inputKey: 0x006A /* 'j' */, caretPosition: 4, expectedOutput: "việt"),
        KeyInputTestCase(existingText: "hươc", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "hước"),
        KeyInputTestCase(existingText: "hoach", inputKey: 0x006A /* 'j' */, caretPosition: 5, expectedOutput: "hoạch"),
        KeyInputTestCase(existingText: "tiêm", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "tiếm"),
        KeyInputTestCase(existingText: "tiên", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "tiến"),
        KeyInputTestCase(existingText: "tiêng", inputKey: 0x0073 /* 's' */, caretPosition: 5, expectedOutput: "tiếng"),
        KeyInputTestCase(existingText: "bênh", inputKey: 0x006A /* 'j' */, caretPosition: 4, expectedOutput: "bệnh"),

        // Disallowed codas (b, d, bare g, bare h, k, l, v, z, or multi-consonant clusters) do not place marks
        KeyInputTestCase(existingText: "cab", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "cabs"),
        KeyInputTestCase(existingText: "bad", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "bads"),
        KeyInputTestCase(existingText: "bag", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "bags"),
        KeyInputTestCase(existingText: "dog", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "dogs"),
        KeyInputTestCase(existingText: "oh", inputKey: 0x0073 /* 's' */, caretPosition: 2, expectedOutput: "ohs"),
        KeyInputTestCase(existingText: "bak", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "baks"),
        KeyInputTestCase(existingText: "book", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "books"),
        KeyInputTestCase(existingText: "cal", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "cals"),
        KeyInputTestCase(existingText: "call", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "calls"),
        KeyInputTestCase(existingText: "lov", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "lovs"),
        KeyInputTestCase(existingText: "baz", inputKey: 0x0073 /* 's' */, caretPosition: 3, expectedOutput: "bazs"),
        KeyInputTestCase(existingText: "cant", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "cants"),
        KeyInputTestCase(existingText: "hand", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "hands"),
        KeyInputTestCase(existingText: "camp", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "camps"),
        KeyInputTestCase(existingText: "bank", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "banks"),
        KeyInputTestCase(existingText: "back", inputKey: 0x0073 /* 's' */, caretPosition: 4, expectedOutput: "backs"),
        KeyInputTestCase(existingText: "point", inputKey: 0x0073 /* 's' */, caretPosition: 5, expectedOutput: "points"),
        KeyInputTestCase(existingText: "del", inputKey: 0x0065 /* 'e' */, caretPosition: 3, expectedOutput: "dele"),
        KeyInputTestCase(existingText: "dev", inputKey: 0x0065 /* 'e' */, caretPosition: 3, expectedOutput: "deve"),
        KeyInputTestCase(existingText: "lev", inputKey: 0x0065 /* 'e' */, caretPosition: 3, expectedOutput: "leve"),
        KeyInputTestCase(existingText: "nev", inputKey: 0x0065 /* 'e' */, caretPosition: 3, expectedOutput: "neve"),
        KeyInputTestCase(existingText: "sol", inputKey: 0x006F /* 'o' */, caretPosition: 3, expectedOutput: "solo"),
        KeyInputTestCase(existingText: "gal", inputKey: 0x0061 /* 'a' */, caretPosition: 3, expectedOutput: "gala"),
        KeyInputTestCase(existingText: "lav", inputKey: 0x0061 /* 'a' */, caretPosition: 3, expectedOutput: "lava"),
        KeyInputTestCase(existingText: "hak", inputKey: 0x0077 /* 'w' */, caretPosition: 3, expectedOutput: "hakw"),

        // 12. 'ưo' + 'w' -> 'ươ' (including case preservation, valid codas/offglides, and undo)
        KeyInputTestCase(existingText: "ưo", inputKey: 0x0077 /* 'w' */, caretPosition: 2, expectedOutput: "ươ"),
        KeyInputTestCase(existingText: "ƯO", inputKey: 0x0077 /* 'w' */, caretPosition: 2, expectedOutput: "ƯƠ"),
        KeyInputTestCase(existingText: "Ưo", inputKey: 0x0077 /* 'w' */, caretPosition: 2, expectedOutput: "Ươ"),
        KeyInputTestCase(existingText: "tưo", inputKey: 0x0077 /* 'w' */, caretPosition: 3, expectedOutput: "tươ"),
        KeyInputTestCase(existingText: "hưo", inputKey: 0x0077 /* 'w' */, caretPosition: 3, expectedOutput: "hươ"),
        KeyInputTestCase(existingText: "tưong", inputKey: 0x0077 /* 'w' */, caretPosition: 5, expectedOutput: "tương"),
        KeyInputTestCase(existingText: "ngưoi", inputKey: 0x0077 /* 'w' */, caretPosition: 5, expectedOutput: "ngươi"),
        KeyInputTestCase(existingText: "ưoc", inputKey: 0x0077 /* 'w' */, caretPosition: 3, expectedOutput: "ươc"),
        KeyInputTestCase(existingText: "ươ", inputKey: 0x0077 /* 'w' */, caretPosition: 2, expectedOutput: "uow"),
        KeyInputTestCase(existingText: "fưo", inputKey: 0x0077 /* 'w' */, caretPosition: 3, expectedOutput: "fưow"),
        KeyInputTestCase(existingText: "tưos", inputKey: 0x0077 /* 'w' */, caretPosition: 4, expectedOutput: "tưosw"),

        // 13. 'ư' + 'o' -> 'ươ' guarded behind `uoHorn` flag (disabled by default)
        KeyInputTestCase(existingText: "ư", inputKey: 0x006F /* 'o' */, caretPosition: 1, uoHorn: false, expectedOutput: "ưo"),
        KeyInputTestCase(existingText: "tư", inputKey: 0x006F /* 'o' */, caretPosition: 2, uoHorn: false, expectedOutput: "tưo"),
        KeyInputTestCase(existingText: "như", inputKey: 0x006F /* 'o' */, caretPosition: 3, uoHorn: false, expectedOutput: "nhưo"),
        KeyInputTestCase(existingText: "ư", inputKey: 0x006F /* 'o' */, caretPosition: 1, uoHorn: true, expectedOutput: "ươ"),
        KeyInputTestCase(existingText: "Ư", inputKey: 0x004F /* 'O' */, caretPosition: 1, uoHorn: true, expectedOutput: "ƯƠ"),
        KeyInputTestCase(existingText: "Ư", inputKey: 0x006F /* 'o' */, caretPosition: 1, uoHorn: true, expectedOutput: "ƯƠ"),
        KeyInputTestCase(existingText: "tư", inputKey: 0x006F /* 'o' */, caretPosition: 2, uoHorn: true, expectedOutput: "tươ"),
        KeyInputTestCase(existingText: "TƯ", inputKey: 0x004F /* 'O' */, caretPosition: 2, uoHorn: true, expectedOutput: "TƯƠ"),
        KeyInputTestCase(existingText: "hư", inputKey: 0x006F /* 'o' */, caretPosition: 2, uoHorn: true, expectedOutput: "hươ"),
        KeyInputTestCase(existingText: "như", inputKey: 0x006F /* 'o' */, caretPosition: 3, uoHorn: true, expectedOutput: "nhươ"),
        KeyInputTestCase(existingText: "ngư", inputKey: 0x006F /* 'o' */, caretPosition: 3, uoHorn: true, expectedOutput: "ngươ"),
        KeyInputTestCase(existingText: "fư", inputKey: 0x006F /* 'o' */, caretPosition: 2, uoHorn: true, expectedOutput: "fưo"),

        // 14. 'ng' + 'i'/'e' -> 'nghi'/'nghe' guarded behind `ngToNgh` flag (disabled by default)
        KeyInputTestCase(existingText: "ng", inputKey: 0x0069 /* 'i' */, caretPosition: 2, ngToNgh: false, expectedOutput: "ngi"),
        KeyInputTestCase(existingText: "NG", inputKey: 0x0049 /* 'I' */, caretPosition: 2, ngToNgh: false, expectedOutput: "NGI"),
        KeyInputTestCase(existingText: "ng", inputKey: 0x0065 /* 'e' */, caretPosition: 2, ngToNgh: false, expectedOutput: "nge"),
        KeyInputTestCase(existingText: "NG", inputKey: 0x0045 /* 'E' */, caretPosition: 2, ngToNgh: false, expectedOutput: "NGE"),
        KeyInputTestCase(existingText: "ng", inputKey: 0x0069 /* 'i' */, caretPosition: 2, ngToNgh: true, expectedOutput: "nghi"),
        KeyInputTestCase(existingText: "NG", inputKey: 0x0049 /* 'I' */, caretPosition: 2, ngToNgh: true, expectedOutput: "NGHI"),
        KeyInputTestCase(existingText: "Ng", inputKey: 0x0069 /* 'i' */, caretPosition: 2, ngToNgh: true, expectedOutput: "Nghi"),
        KeyInputTestCase(existingText: "Ng", inputKey: 0x0049 /* 'I' */, caretPosition: 2, ngToNgh: true, expectedOutput: "NghI"),
        KeyInputTestCase(existingText: "ng", inputKey: 0x0065 /* 'e' */, caretPosition: 2, ngToNgh: true, expectedOutput: "nghe"),
        KeyInputTestCase(existingText: "NG", inputKey: 0x0045 /* 'E' */, caretPosition: 2, ngToNgh: true, expectedOutput: "NGHE"),
        KeyInputTestCase(existingText: "Ng", inputKey: 0x0065 /* 'e' */, caretPosition: 2, ngToNgh: true, expectedOutput: "Nghe"),
        KeyInputTestCase(existingText: "Ng", inputKey: 0x0045 /* 'E' */, caretPosition: 2, ngToNgh: true, expectedOutput: "NghE"),
        KeyInputTestCase(existingText: "ang", inputKey: 0x0069 /* 'i' */, caretPosition: 3, ngToNgh: true, expectedOutput: "angi"),
        KeyInputTestCase(existingText: "ang", inputKey: 0x0065 /* 'e' */, caretPosition: 3, ngToNgh: true, expectedOutput: "ange"),

        // 15. 'ngh' + 'a'/'o'/'u' -> 'nga'/'ngo'/'ngu' guarded behind `nghToNg` flag (disabled by default)
        KeyInputTestCase(existingText: "ngh", inputKey: 0x0061 /* 'a' */, caretPosition: 3, nghToNg: false, expectedOutput: "ngha"),
        KeyInputTestCase(existingText: "ngh", inputKey: 0x006F /* 'o' */, caretPosition: 3, nghToNg: false, expectedOutput: "ngho"),
        KeyInputTestCase(existingText: "ngh", inputKey: 0x0075 /* 'u' */, caretPosition: 3, nghToNg: false, expectedOutput: "nghu"),
        KeyInputTestCase(existingText: "ngh", inputKey: 0x0061 /* 'a' */, caretPosition: 3, nghToNg: true, expectedOutput: "nga"),
        KeyInputTestCase(existingText: "ngh", inputKey: 0x006F /* 'o' */, caretPosition: 3, nghToNg: true, expectedOutput: "ngo"),
        KeyInputTestCase(existingText: "ngh", inputKey: 0x0075 /* 'u' */, caretPosition: 3, nghToNg: true, expectedOutput: "ngu"),
        KeyInputTestCase(existingText: "NGH", inputKey: 0x0041 /* 'A' */, caretPosition: 3, nghToNg: true, expectedOutput: "NGA"),
        KeyInputTestCase(existingText: "NGH", inputKey: 0x004F /* 'O' */, caretPosition: 3, nghToNg: true, expectedOutput: "NGO"),
        KeyInputTestCase(existingText: "NGH", inputKey: 0x0055 /* 'U' */, caretPosition: 3, nghToNg: true, expectedOutput: "NGU"),
        KeyInputTestCase(existingText: "Ngh", inputKey: 0x0061 /* 'a' */, caretPosition: 3, nghToNg: true, expectedOutput: "Nga"),
        KeyInputTestCase(existingText: "Ngh", inputKey: 0x006F /* 'o' */, caretPosition: 3, nghToNg: true, expectedOutput: "Ngo"),
        KeyInputTestCase(existingText: "Ngh", inputKey: 0x0075 /* 'u' */, caretPosition: 3, nghToNg: true, expectedOutput: "Ngu")
    ])
    func testKeyInputTuples(testCase: KeyInputTestCase) async throws {
        let result = InputMethodBuffer.processKey(
            existingText: testCase.existingText,
            caretPosition: testCase.caretPosition,
            inputKey: testCase.inputKey,
            modernTone: testCase.modernTone,
            uoHorn: testCase.uoHorn,
            ngToNgh: testCase.ngToNgh,
            nghToNg: testCase.nghToNg
        )
        #expect(result == testCase.expectedOutput, "Failed for [\(testCase.description)]: got '\(result)', expected '\(testCase.expectedOutput)'")
    }

    @MainActor
    @Test("Verify strict VIQR sequential word typing")
    func testStrictVIQRTyping() {
        // Verify sequential word typing with strict immediate vowel modifiers and tones
        func simulateVIQR(_ keystrokes: String) -> String {
            let ime = InputMethodBuffer(characters: [], pos: 0, style: .viqr)
            var screen: [UniChar] = []
            for ch in keystrokes.utf16 {
                if let action = ime.handleInput([ch]) {
                    if action.backspaceCount > 0 {
                        screen.removeLast(action.backspaceCount)
                    }
                    screen.append(contentsOf: action.text.utf16)
                } else {
                    screen.append(ch)
                }
            }
            return String(utf16CodeUnits: screen, count: screen.count)
        }

        #expect(simulateVIQR("tiê'ng viê.t") == "tiếng việt")
        #expect(simulateVIQR("tie^'ng vie^.t") == "tiếng việt")
        #expect(simulateVIQR("ddo^`ng") == "đồng")
        #expect(simulateVIQR("nu+o+'c") == "nước")
        #expect(simulateVIQR("nưo*'") == "nướ")
        #expect(simulateVIQR("a(n") == "ăn")
        #expect(simulateVIQR("a('n") == "ắn")
        // Tones/modifiers typed AFTER final consonants must NOT modify the preceding vowel in strict mode
        #expect(simulateVIQR("tieng'") == "tieng'")
        #expect(simulateVIQR("viet.") == "viet.")
        #expect(simulateVIQR("can^") == "can^")
        #expect(simulateVIQR("can(") == "can(")
        #expect(simulateVIQR("con+") == "con+")
    }

    @MainActor
    @Test("Verify VNI sequential word typing")
    func testVNITyping() {
        func simulateVNI(_ keystrokes: String, uoHorn: Bool = false, ngToNgh: Bool = false, nghToNg: Bool = false) -> String {
            let ime = InputMethodBuffer(characters: [], pos: 0, style: .vni, uoHorn: uoHorn, ngToNgh: ngToNgh, nghToNg: nghToNg)
            var screen: [UniChar] = []
            for ch in keystrokes.utf16 {
                if let action = ime.handleInput([ch]) {
                    if action.backspaceCount > 0 {
                        screen.removeLast(action.backspaceCount)
                    }
                    screen.append(contentsOf: action.text.utf16)
                } else {
                    screen.append(ch)
                }
            }
            return String(utf16CodeUnits: screen, count: screen.count)
        }

        #expect(simulateVNI("xin cha2o") == "xin chào")
        #expect(simulateVNI("tie61ng vie65t") == "tiếng việt")
        #expect(simulateVNI("tieng61 viet65") == "tiếng việt")
        #expect(simulateVNI("d9o62ng") == "đồng")
        #expect(simulateVNI("dong962") == "đồng")
        #expect(simulateVNI("nu7o71c") == "nước")
        #expect(simulateVNI("nuoc71") == "nước")
        #expect(simulateVNI("nu7o1c", uoHorn: true) == "nước")
        #expect(simulateVNI("ngie61ng", ngToNgh: true) == "nghiếng")
        #expect(simulateVNI("ngha1", nghToNg: true) == "ngá")
        #expect(simulateVNI("hoa2 khoe3 thuy1 hoa2n") == "hòa khỏe thúy hoàn")

        let modernVNI = InputMethodBuffer(characters: [], pos: 0, style: .vni, modernTone: true)
        var modernScreen: [UniChar] = []
        for ch in "hoa2 khoe3 thuy1 hoa2n".utf16 {
            if let action = modernVNI.handleInput([ch]) {
                if action.backspaceCount > 0 { modernScreen.removeLast(action.backspaceCount) }
                modernScreen.append(contentsOf: action.text.utf16)
            } else {
                modernScreen.append(ch)
            }
        }
        #expect(String(utf16CodeUnits: modernScreen, count: modernScreen.count) == "hoà khoẻ thuý hoàn")
    }

    @MainActor
    @Test("Multi-keystroke word-based typing across spaces and complex syllables")
    func testSequentialWordTyping() {
        func simulateTyping(_ keystrokes: String, uoHorn: Bool = false, ngToNgh: Bool = false, nghToNg: Bool = false) -> String {
            let ime = InputMethodBuffer(characters: [], pos: 0, uoHorn: uoHorn, ngToNgh: ngToNgh, nghToNg: nghToNg)
            var screen: [UniChar] = []
            for ch in keystrokes.utf16 {
                if let action = ime.handleInput([ch]) {
                    if action.backspaceCount > 0 {
                        screen.removeLast(action.backspaceCount)
                    }
                    screen.append(contentsOf: action.text.utf16)
                } else {
                    screen.append(ch)
                }
            }
            return String(utf16CodeUnits: screen, count: screen.count)
        }

        #expect(simulateTyping("xin chaof") == "xin chào")
        #expect(simulateTyping("tieengs vieetj") == "tiếng việt")
        #expect(simulateTyping("thuowr xua") == "thuở xua")
        #expect(simulateTyping("ngoaif giauf nghieengs khuyeens") == "ngoài giàu nghiếng khuyến")
        #expect(simulateTyping("uwo nuwocs nguwoif tuwong uwocs") == "ưo nưóc ngưòi tưong ưóc")
        #expect(simulateTyping("uwo nuwocs nguwoif tuwong uwocs", uoHorn: true) == "ươ nước người tương ước")
        #expect(simulateTyping("uwow nuwowcs nguwowif tuwowng uwowcs") == "ươ nước người tương ước")
        #expect(simulateTyping("ngix ngieengs nges ngees", ngToNgh: true) == "nghĩ nghiếng nghé nghế")
        #expect(simulateTyping("nghas nghof nghus nghuowif", uoHorn: true, nghToNg: true) == "ngá ngò ngú người")
        #expect(simulateTyping("first firsts carrs cossts surrf") == "first firsts cars costs surf")
        #expect(simulateTyping("resset forward sofftware passsword esscape") == "reset forward software password escape")
        #expect(simulateTyping("cowws dowwns news awwake powwer") == "cows downs news awake power")
        #expect(simulateTyping("was free three school website welcome camera potato decide david") == "was free three school website welcome camera potato decide david")
        #expect(simulateTyping("dogs kids calls hands points camps banks delete select develop level never solo") == "dogs kids calls hands points camps banks delete select develop level never solo")
    }

    @MainActor
    @Test("Modifier key combinations (Cmd, Ctrl, Option, Shift+Arrow) reset buffer instead of combining")
    func testModifierKeyCombinationsResetBuffer() {
        // 1. Typing 'a', then Cmd+A (select all), then 'a' should not combine into 'â'
        let imeCmd = InputMethodBuffer(characters: [], pos: 0)
        #expect(imeCmd.handleInput([0x0061 /* 'a' */]) == nil)
        #expect(imeCmd.handleInput([0x0061 /* 'a' */], flags: .maskCommand) == nil)
        // After Cmd+A resets the buffer, typing 'a' starts fresh rather than combining with the previous 'a'
        #expect(imeCmd.handleInput([0x0061 /* 'a' */]) == nil)

        // 2. Typing 'o', then Ctrl+A (beginning of line), then 'w' should not combine into 'ơ'
        let imeCtrl = InputMethodBuffer(characters: [], pos: 0)
        #expect(imeCtrl.handleInput([0x006F /* 'o' */]) == nil)
        #expect(imeCtrl.handleInput([0x0061 /* 'a' */], flags: .maskControl) == nil)
        #expect(imeCtrl.handleInput([0x0077 /* 'w' */]) == nil)

        // 3. Typing 'c', 'o', then Option+Left, then 's' should not combine into 'có'
        let imeOpt = InputMethodBuffer(characters: [], pos: 0)
        #expect(imeOpt.handleInput([0x0063 /* 'c' */]) == nil)
        #expect(imeOpt.handleInput([0x006F /* 'o' */]) == nil)
        #expect(imeOpt.handleInput([leftArrowKey], flags: .maskAlternate) == nil)
        #expect(imeOpt.handleInput([0x0073 /* 's' */]) == nil)

        // 4. Typing 'c', 'o', then Shift+Left (text selection), then 's' should not combine into 'có'
        let imeShiftArrow = InputMethodBuffer(characters: [], pos: 0)
        #expect(imeShiftArrow.handleInput([0x0063 /* 'c' */]) == nil)
        #expect(imeShiftArrow.handleInput([0x006F /* 'o' */]) == nil)
        #expect(imeShiftArrow.handleInput([leftArrowKey], flags: .maskShift) == nil)
        #expect(imeShiftArrow.handleInput([0x0073 /* 's' */]) == nil)
    }

    @MainActor
    @Test("Switch InputMethodStyle dynamically between Telex, VNI, and VIQR")
    func testSwitchInputMethodStyle() {
        let ime = InputMethodBuffer(characters: [], pos: 0, uoHorn: true, ngToNgh: true, nghToNg: true)
        #expect(ime.style == .telex)

        func typeWord(_ keystrokes: String) -> String {
            ime.reset()
            var screen: [UniChar] = []
            for ch in keystrokes.utf16 {
                if let action = ime.handleInput([ch]) {
                    if action.backspaceCount > 0 {
                        screen.removeLast(action.backspaceCount)
                    }
                    screen.append(contentsOf: action.text.utf16)
                } else {
                    screen.append(ch)
                }
            }
            return String(utf16CodeUnits: screen, count: screen.count)
        }

        // In Telex mode: "tieengs" -> "tiếng", "nuwocs" (with uoHorn) -> "nước"
        #expect(typeWord("tieengs") == "tiếng")
        #expect(typeWord("nuwocs") == "nước")

        // Switch to VIQR mode and verify VIQR typing + autocorrection flags (uoHorn, ngToNgh, nghToNg)
        ime.style = .viqr
        #expect(ime.style == .viqr)
        #expect(typeWord("tie^'ng") == "tiếng")
        #expect(typeWord("nuo+'c") == "nước")
        #expect(typeWord("nu+o'c") == "nước")
        #expect(typeWord("ngu+o`i") == "người")
        #expect(typeWord("ngie^'ng") == "nghiếng")
        #expect(typeWord("nge'") == "nghé")
        #expect(typeWord("ngha'") == "ngá")
        #expect(typeWord("nghu+o`i") == "người")
        // When uoHorn is disabled in VIQR mode, "nu+o'c" stays "nưóc" and "nu+o+'c" becomes "nước"
        ime.uoHorn = false
        #expect(typeWord("nu+o'c") == "nưóc")
        #expect(typeWord("nu+o+'c") == "nước")
        ime.uoHorn = true
        // Telex sequence should stay literal in VIQR mode
        #expect(typeWord("tieengs") == "tieengs")

        // Switch to VNI mode and verify VNI typing + autocorrection flags (uoHorn, ngToNgh, nghToNg)
        ime.style = .vni
        #expect(ime.style == .vni)
        #expect(typeWord("tieng61") == "tiếng")
        #expect(typeWord("nuoc71") == "nước")
        #expect(typeWord("nu7o1c") == "nước")
        #expect(typeWord("ngie61ng") == "nghiếng")
        #expect(typeWord("ngha1") == "ngá")
        // Telex sequence should stay literal in VNI mode
        #expect(typeWord("tieengs") == "tieengs")

        // Switch back to Telex mode and verify flags (uoHorn, ngToNgh, nghToNg) are preserved
        ime.style = .telex
        #expect(ime.style == .telex)
        #expect(typeWord("nuwocs") == "nước")
        #expect(typeWord("ngieengs") == "nghiếng")
        #expect(typeWord("nghas") == "ngá")
    }

    @MainActor
    @Test("Holding both Left Shift and Right Shift restores the current word to its raw typed characters")
    func testDualShiftRestoresCurrentWordToRawKeystrokes() {
        let ime = InputMethodBuffer(characters: [], pos: 0, uoHorn: true)
        var dualShift = DualShiftDetector()
        var screen: [UniChar] = []

        func applyAction(_ action: InputMethodBuffer.Action) {
            if action.backspaceCount > 0 {
                screen.removeLast(action.backspaceCount)
            }
            screen.append(contentsOf: action.text.utf16)
        }

        func typeText(_ text: String) {
            for ch in text.utf16 {
                if let action = ime.handleInput([ch]) {
                    applyAction(action)
                } else if ch == backspaceKey {
                    if !screen.isEmpty {
                        screen.removeLast()
                    }
                } else {
                    screen.append(ch)
                }
            }
        }

        func pressDualShift() {
            // Press Left Shift down (56), then Right Shift down (60) while Shift mask is active
            let leftDown = dualShift.shouldTriggerRestore(
                keyCode: KeyCodes.leftShiftKeyCode,
                flags: .maskShift
            )
            #expect(!leftDown)

            let rightDown = dualShift.shouldTriggerRestore(
                keyCode: KeyCodes.rightShiftKeyCode,
                flags: .maskShift
            )
            #expect(rightDown)
            if rightDown, let action = ime.restoreCurrentWord() {
                applyAction(action)
            }

            // Release both Shift keys
            let rightUp = dualShift.shouldTriggerRestore(
                keyCode: KeyCodes.rightShiftKeyCode,
                flags: .maskShift
            )
            #expect(!rightUp)

            let leftUp = dualShift.shouldTriggerRestore(
                keyCode: KeyCodes.leftShiftKeyCode,
                flags: []
            )
            #expect(!leftUp)
        }

        func currentScreen() -> String {
            String(utf16CodeUnits: screen, count: screen.count)
        }

        // 1. Type "tieengs" -> "tiếng", then hold both Shift keys -> restored to "tieengs"
        typeText("tieengs")
        #expect(currentScreen() == "tiếng")
        pressDualShift()
        #expect(currentScreen() == "tieengs")

        // 2. Further characters in the same restored word are inserted literally without Telex transforms
        typeText("s")
        #expect(currentScreen() == "tieengss")

        // 3. After a word boundary (space), the next word transforms normally and can also be restored
        typeText(" vieet")
        #expect(currentScreen() == "tieengss viêt")
        pressDualShift()
        #expect(currentScreen() == "tieengss vieet")
        typeText("s")
        #expect(currentScreen() == "tieengss vieets")

        // 4. Works across VNI as well ("tieng61" -> "tiếng", dual-Shift -> "tieng61")
        screen.removeAll()
        ime.style = .vni
        typeText("tieng61")
        #expect(currentScreen() == "tiếng")
        pressDualShift()
        #expect(currentScreen() == "tieng61")

        // 5. Typing space after a word, backspacing the space, and pressing dual-Shift still restores
        screen.removeAll()
        ime.style = .telex
        typeText("roof ")
        #expect(currentScreen() == "rồ ")
        typeText("\u{007F}")
        #expect(currentScreen() == "rồ")
        pressDualShift()
        #expect(currentScreen() == "roof")
        typeText("s vieet")
        #expect(currentScreen() == "roofs viêt")

        // 6. Backspacing inside a transformed word and pressing dual-Shift restores remaining word
        screen.removeAll()
        ime.reset()
        typeText("tieengs\u{007F}")
        #expect(currentScreen() == "tiến")
        pressDualShift()
        #expect(currentScreen() == "tieens")

        // 7. Pressing dual-Shift AFTER ending a word with space or punctuation restores the ended
        // word while preserving trailing boundaries and allowing the next word to transform.
        screen.removeAll()
        ime.reset()
        typeText("moojt roof, ")
        #expect(currentScreen() == "một rồ, ")
        pressDualShift()
        #expect(currentScreen() == "một roof, ")
        pressDualShift()
        #expect(currentScreen() == "một roof, ")
        typeText("mowis")
        #expect(currentScreen() == "một roof, mới")
    }

    @MainActor
    @Test("Configurable maxBufferSize retains words across spaces for backspacing when surrounding text is unavailable")
    func testConfigurableMaxBufferSizeForBackspacingAcrossWords() {
        let ime = InputMethodBuffer(characters: [], pos: 0, maxBufferSize: 16)
        var screen: [UniChar] = []

        func typeChar(_ ch: UniChar) {
            if let action = ime.handleInput([ch]) {
                if action.backspaceCount > 0 {
                    screen.removeLast(action.backspaceCount)
                }
                screen.append(contentsOf: action.text.utf16)
            } else if ch == backspaceKey {
                if !screen.isEmpty {
                    screen.removeLast()
                }
            } else {
                screen.append(ch)
            }
        }

        func typeText(_ text: String) {
            for ch in text.utf16 {
                typeChar(ch)
            }
        }

        func pressBackspace(_ count: Int = 1) {
            for _ in 0..<count {
                typeChar(backspaceKey)
            }
        }

        func currentScreen() -> String {
            String(utf16CodeUnits: screen, count: screen.count)
        }

        // 1. Type "co " ("co " is 3 chars, retained in the 16-char buffer), then backspace the space and type 's' -> "có"
        typeText("co ")
        #expect(currentScreen() == "co ")
        pressBackspace(1)
        #expect(currentScreen() == "co")
        typeText("s")
        #expect(currentScreen() == "có")

        // 2. Type " tieeng" ("có tiêng"), backspace 6 chars (" tiêng") back to "có", then type 'z' -> removes tone to "co"
        typeText(" tieeng")
        #expect(currentScreen() == "có tiêng")
        pressBackspace(6) // deletes " tiêng" -> "có"
        #expect(currentScreen() == "có")
        typeText("z")
        #expect(currentScreen() == "co")

        // 3. When total typed characters exceed maxBufferSize (10), older prefix characters beyond maxBufferSize are trimmed
        screen.removeAll()
        ime.reset()
        ime.maxBufferSize = 10
        // Type "co 1234567890" (13 chars total; buffer only keeps the last 10 chars "1234567890")
        typeText("co 1234567890")
        #expect(currentScreen() == "co 1234567890")
        // Backspacing 11 times deletes " 1234567890", exhausting the 10-char buffer so 'co' is no longer in buffer
        pressBackspace(11)
        #expect(currentScreen() == "co")
        typeText("s")
        #expect(currentScreen() == "cos")
    }

    @MainActor
    @Test("AppLanguage resolves system language with Vietnamese fallback and loads English and Vietnamese catalogs")
    func testAppLanguageLocalizationAndFallback() {
        #expect(AppLanguage.system.resolvedLanguageCode(preferredLanguages: ["en-US", "vi-VN"]) == "en")
        #expect(AppLanguage.system.resolvedLanguageCode(preferredLanguages: ["vi-VN", "en-US"]) == "vi")
        #expect(AppLanguage.system.resolvedLanguageCode(preferredLanguages: ["ja-JP", "fr-FR"]) == "vi")
        #expect(AppLanguage.system.resolvedLanguageCode(preferredLanguages: []) == "vi")

        #expect(AppLanguage.english.localized("settings.section.general") == "General")
        #expect(AppLanguage.vietnamese.localized("settings.section.general") == "Thường gặp")
        #expect(AppLanguage.english.localized("settings.section.correction") == "Tones & Spelling")
        #expect(AppLanguage.vietnamese.localized("settings.section.correction") == "Bỏ dấu & Chính tả")
        #expect(AppLanguage.english.localized("menu.input_source_warning.title", "ABC") == "⚠️ Keyboard: ABC")
        #expect(AppLanguage.vietnamese.localized("menu.input_source_warning.title", "ABC") == "⚠️ Bàn phím: ABC")
        #expect(AppLanguage.system.displayName(in: .english) == "System Default")
        #expect(AppLanguage.system.displayName(in: .vietnamese) == "Theo hệ thống")

        #expect(MenuBarStatusView.tooltipText(style: .telex, language: .english) == "ÔKêyTre: Telex")
        #expect(MenuBarStatusView.tooltipText(style: .off, language: .english) == "ÔKêyTre: Off")
        #expect(MenuBarStatusView.tooltipText(style: .off, language: .vietnamese) == "ÔKêyTre: Tắt")
    }

    @Test("PermissionManager resets stale TCC only once per ad-hoc CDHash change and skips Team-signed builds")
    func testAdHocTCCResetOnBinaryChange() {
        let suiteName = "com.okeytre.tests.tcc.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suiteName)!
        defer { defaults.removePersistentDomain(forName: suiteName) }

        var resetCalls: [String] = []

        let v1AdHoc = CodeSigningIdentity(
            bundleIdentifier: "com.okeytre.OKeyTre",
            cdHashHex: "1111aaaa",
            teamIdentifier: nil
        )
        let v2AdHoc = CodeSigningIdentity(
            bundleIdentifier: "com.okeytre.OKeyTre",
            cdHashHex: "2222bbbb",
            teamIdentifier: nil
        )
        let teamSigned = CodeSigningIdentity(
            bundleIdentifier: "com.okeytre.OKeyTre",
            cdHashHex: "3333cccc",
            teamIdentifier: "TEAM123456"
        )

        // 1. First launch / upgrade from pre-tracking v0.1 resets stale TCC once and records hash
        let didResetV1 = PermissionManager.resetStaleAdHocAccessibilityIfNeeded(
            identity: v1AdHoc,
            userDefaults: defaults,
            tccResetRunner: { resetCalls.append($0) }
        )
        #expect(didResetV1 == true)
        #expect(resetCalls == ["com.okeytre.OKeyTre"])
        #expect(defaults.string(forKey: PermissionManager.lastRegisteredCDHashDefaultsKey) == "1111aaaa")

        // 2. Re-launching the exact same binary while still untrusted does NOT reset again
        let didResetSameV1 = PermissionManager.resetStaleAdHocAccessibilityIfNeeded(
            identity: v1AdHoc,
            userDefaults: defaults,
            tccResetRunner: { resetCalls.append($0) }
        )
        #expect(didResetSameV1 == false)
        #expect(resetCalls.count == 1)

        // 3. Upgrading to a new ad-hoc release (new CDHash) resets stale TCC once more
        let didResetV2 = PermissionManager.resetStaleAdHocAccessibilityIfNeeded(
            identity: v2AdHoc,
            userDefaults: defaults,
            tccResetRunner: { resetCalls.append($0) }
        )
        #expect(didResetV2 == true)
        #expect(resetCalls == ["com.okeytre.OKeyTre", "com.okeytre.OKeyTre"])
        #expect(defaults.string(forKey: PermissionManager.lastRegisteredCDHashDefaultsKey) == "2222bbbb")

        // 4. Team-signed builds (Apple Development / Developer ID) never trigger tccutil reset
        let didResetTeam = PermissionManager.resetStaleAdHocAccessibilityIfNeeded(
            identity: teamSigned,
            userDefaults: defaults,
            tccResetRunner: { resetCalls.append($0) }
        )
        #expect(didResetTeam == false)
        #expect(resetCalls.count == 2)

        // 5. Already-trusted build updates stored CDHash without resetting
        let shouldResetTrusted = PermissionManager.shouldResetStaleAdHocTCC(
            identity: CodeSigningIdentity(
                bundleIdentifier: "com.okeytre.OKeyTre",
                cdHashHex: "4444dddd",
                teamIdentifier: nil
            ),
            isTrusted: true,
            userDefaults: defaults
        )
        #expect(shouldResetTrusted == false)
        #expect(defaults.string(forKey: PermissionManager.lastRegisteredCDHashDefaultsKey) == "4444dddd")

        // 6. Live test-host binary exposes a valid CodeSigningIdentity
        let current = CodeSigningIdentity.current()
        #expect(current != nil)
        #expect(current?.cdHashHex.isEmpty == false)
    }
}
