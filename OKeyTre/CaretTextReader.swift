// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import ApplicationServices
import CoreFoundation
import Foundation

enum CaretTextReader {
    struct SurroundingText {
        let wordUTF16: [UniChar]
        let caretOffset: Int
    }

    /// Per-element messaging timeout (50ms) so slow web renderers never stall the `CGEventTap`.
    private static let messagingTimeoutSeconds: Float = 0.05

    /// Shared system-wide accessibility element with a short global messaging timeout for this process.
    private static let systemWide: AXUIElement = {
        let element = AXUIElementCreateSystemWide()
        AXUIElementSetMessagingTimeout(element, messagingTimeoutSeconds)
        return element
    }()

    /// Tracks PIDs for which we have already enabled `AXManualAccessibility` / `AXEnhancedUserInterface`.
    private static var enhancedPids = Set<pid_t>()

    private static func attribute(_ attr: String, of element: AXUIElement) -> CFTypeRef? {
        var ref: CFTypeRef?
        guard AXUIElementCopyAttributeValue(element, attr as CFString, &ref) == .success else {
            return nil
        }
        return ref
    }

    private static func elementAttribute(_ attr: String, of element: AXUIElement) -> AXUIElement? {
        guard let ref = attribute(attr, of: element),
              CFGetTypeID(ref) == AXUIElementGetTypeID() else {
            return nil
        }
        return (ref as! AXUIElement)
    }

    private static func parameterizedAttribute(
        _ attr: String,
        parameter: CFTypeRef,
        of element: AXUIElement
    ) -> CFTypeRef? {
        var ref: CFTypeRef?
        guard AXUIElementCopyParameterizedAttributeValue(
            element,
            attr as CFString,
            parameter,
            &ref
        ) == .success else {
            return nil
        }
        return ref
    }

    private static func cfRange(from value: CFTypeRef?) -> CFRange? {
        guard let value, CFGetTypeID(value) == AXValueGetTypeID() else { return nil }
        var range = CFRange()
        return AXValueGetValue(value as! AXValue, .cfRange, &range) ? range : nil
    }

    /// Enables `AXManualAccessibility` (for Electron apps like VS Code) and `AXEnhancedUserInterface`
    /// (for Chromium browsers and Apple iWork apps like Pages/Keynote/Numbers) once per process.
    static func enableEnhancedUserInterface(for pid: pid_t) {
        _ = systemWide
        guard pid > 0,
              pid != ProcessInfo.processInfo.processIdentifier,
              !enhancedPids.contains(pid) else { return }

        let appElement = AXUIElementCreateApplication(pid)
        AXUIElementSetMessagingTimeout(appElement, messagingTimeoutSeconds)
        let manualErr = AXUIElementSetAttributeValue(appElement, "AXManualAccessibility" as CFString, kCFBooleanTrue)
        let enhancedErr = AXUIElementSetAttributeValue(appElement, "AXEnhancedUserInterface" as CFString, kCFBooleanTrue)
        if enhancedErr == .success || enhancedErr == .attributeUnsupported || enhancedErr == .illegalArgument ||
           manualErr == .success {
            enhancedPids.insert(pid)
        }
    }

    static func handleAppTerminated(pid: pid_t) {
        enhancedPids.remove(pid)
    }

    static func resetEnhancedPids() {
        enhancedPids.removeAll()
    }

    private static func supportsTextSelection(_ element: AXUIElement) -> Bool {
        guard let val = attribute(kAXSelectedTextRangeAttribute as String, of: element) else {
            return false
        }
        return CFGetTypeID(val) == AXValueGetTypeID()
    }

    /// Resolves the currently focused UI element, including fallbacks for Apple Pages/iWork and Chromium-based applications.
    static func getFocusedElement() -> AXUIElement? {
        guard !ProcessInfo.isAutomatedEnvironment else { return nil }
        if let focused = elementAttribute(kAXFocusedUIElementAttribute as String, of: systemWide) {
            var pid: pid_t = 0
            if AXUIElementGetPid(focused, &pid) == .success, pid > 0, !enhancedPids.contains(pid),
               !supportsTextSelection(focused) {
                // Apps like Apple Pages return the raw canvas view until AXEnhancedUserInterface is enabled.
                enableEnhancedUserInterface(for: pid)
                let appElement = AXUIElementCreateApplication(pid)
                return elementAttribute(kAXFocusedUIElementAttribute as String, of: appElement)
                    ?? elementAttribute(kAXFocusedUIElementAttribute as String, of: systemWide)
                    ?? focused
            }
            return focused
        }

        // Fallback: Query systemWide focused application, then query its focused element
        if let focusedApp = elementAttribute(kAXFocusedApplicationAttribute as String, of: systemWide) {
            var pid: pid_t = 0
            if AXUIElementGetPid(focusedApp, &pid) == .success, pid > 0 {
                enableEnhancedUserInterface(for: pid)
            }
            return elementAttribute(kAXFocusedUIElementAttribute as String, of: focusedApp)
        }
        return nil
    }

    /// Returns the length of the currently selected text in the focused element, or 0 if no selection.
    static func getSelectedRangeLength(for element: AXUIElement? = nil) -> Int {
        guard let targetElement = element ?? getFocusedElement() else {
            return 0
        }

        // 1. Primary check: kAXSelectedTextRangeAttribute
        if let selectedRange = cfRange(from: attribute(kAXSelectedTextRangeAttribute as String, of: targetElement)),
           isValidCaretLocation(selectedRange.location),
           selectedRange.length > 0,
           selectedRange.length < (CFIndex.max - 1024) {
            return selectedRange.length
        }

        // 2. Secondary check: kAXSelectedTextAttribute (e.g. Chrome Omnibox inline autocomplete)
        if let str = attribute(kAXSelectedTextAttribute as String, of: targetElement) as? String,
           !str.isEmpty {
            return str.utf16.count
        }

        return 0
    }

    private static let editableRoles: Set<String> = [
        kAXTextFieldRole as String,
        kAXTextAreaRole as String,
        kAXComboBoxRole as String,
        "AXSearchField",
    ]

    private static let emptyEditorDOMClasses: Set<String> = [
        "ql-blank",          // Quill (Google Gemini, Slack, LinkedIn, etc.)
        "is-editor-empty",   // ProseMirror / TipTap (Claude, Linear, etc.)
        "ck-placeholder",    // CKEditor 5
    ]

    private static let emptyCaretNodeDOMClasses: Set<String> = [
        "ql-blank",
        "is-editor-empty",
        "is-empty",
        "ck-placeholder",
    ]

    /// Returns true when a DOM `classList` (`AXDOMClassList`) marks the editor or active block
    /// as logically empty (so any text in `AXValue` comes from CSS `::before` / `::after` placeholders).
    static func isEmptyEditorClassList(
        _ classList: [String],
        includeLeafClasses: Bool = false
    ) -> Bool {
        let candidates = includeLeafClasses ? emptyCaretNodeDOMClasses : emptyEditorDOMClasses
        return classList.contains { candidates.contains($0) }
    }

    private static func domClassList(for element: AXUIElement) -> [String]? {
        attribute("AXDOMClassList", of: element) as? [String]
    }

    private static func hasEmptyEditorDOMClass(_ element: AXUIElement) -> Bool {
        var classListRef: CFTypeRef?
        let err = AXUIElementCopyAttributeValue(element, "AXDOMClassList" as CFString, &classListRef)
        if err == .attributeUnsupported {
            return false
        }
        if err == .success,
           let classes = classListRef as? [String],
           isEmptyEditorClassList(classes) {
            return true
        }
        // Also check the immediate parent in case focus sits on an unclassed `<p>` inside `.ql-editor.ql-blank`.
        if let parent = elementAttribute(kAXParentAttribute as String, of: element),
           let parentClasses = domClassList(for: parent),
           isEmptyEditorClassList(parentClasses) {
            return true
        }
        return false
    }

    /// Returns true when `location` is a valid non-negative UTF-16 offset (rejecting `nil`,
    /// negative `kCFNotFound`, and `NSNotFound` / `CFIndex.max` sentinels).
    static func isValidCaretLocation(_ location: Int?) -> Bool {
        guard let location else { return false }
        return location >= 0 && location < (CFIndex.max - 1024)
    }

    /// Returns true when `value` merely mirrors the element's `AXPlaceholderValue`
    /// (ignoring trailing line breaks injected by `contenteditable` `<br>` nodes).
    static func isPlaceholderMatch(value: String, placeholder: String?) -> Bool {
        guard let placeholder else { return false }
        let trimmedPlaceholder = placeholder.trimmingCharacters(in: .newlines)
        guard !trimmedPlaceholder.isEmpty else { return false }
        return value.trimmingCharacters(in: .newlines) == trimmedPlaceholder
    }

    /// Returns true when a caret leaf node's text contains no word characters (e.g. an empty
    /// `<p><br></p>` or zero-width-space placeholder node inside a rich-text `contenteditable`).
    static func isEmptyCaretContainerText(_ text: String) -> Bool {
        text.utf16.allSatisfy { unit in
            okeytre_is_word_boundary(unit)
                || unit == 0x200B // ZERO WIDTH SPACE (Lexical / Slate empty block)
                || unit == 0xFEFF // ZERO WIDTH NO-BREAK SPACE
                || unit == 0xFFFC // OBJECT REPLACEMENT CHARACTER (`<br>` in AX hypertext)
        }
    }

    private static func isEditableTextElement(_ element: AXUIElement) -> Bool {
        if let role = attribute(kAXRoleAttribute as String, of: element) as? String,
           editableRoles.contains(role) {
            return true
        }

        if attribute("AXEditable", of: element) as? Bool == true {
            return true
        }

        for attr in [kAXValueAttribute, kAXSelectedTextRangeAttribute] {
            var isSettable: DarwinBoolean = false
            if AXUIElementIsAttributeSettable(element, attr as CFString, &isSettable) == .success,
               isSettable.boolValue {
                return true
            }
        }
        return false
    }

    /// Extracts the start `AXTextMarker` from an `AXTextMarkerRange` in-process via HIServices
    /// (`AXTextMarkerRangeCopyStartMarker`, supported across Chromium, Firefox, and Safari),
    /// with a fallback to WebKit's `"AXStartTextMarkerForTextMarkerRange"` parameterized attribute.
    static func startTextMarker(
        for markerRange: CFTypeRef,
        in element: AXUIElement? = nil
    ) -> CFTypeRef? {
        if CFGetTypeID(markerRange) == AXTextMarkerRangeGetTypeID() {
            return AXTextMarkerRangeCopyStartMarker(markerRange as! AXTextMarkerRange)
        }
        guard let element else { return nil }
        return parameterizedAttribute("AXStartTextMarkerForTextMarkerRange", parameter: markerRange, of: element)
    }

    private static func stringForTextMarkerRange(
        in element: AXUIElement,
        markerRange: CFTypeRef
    ) -> String? {
        parameterizedAttribute("AXStringForTextMarkerRange", parameter: markerRange, of: element) as? String
    }

    /// Attempts to resolve surrounding text via WebKit / Chromium `AXTextMarker` APIs on `contenteditable`
    /// elements. Returns `.some(nil)` when the caret sits inside an empty paragraph/placeholder leaf
    /// (such as Quill's `.ql-blank` `<p><br></p>` sibling next to a CSS `::before` `"Ask Gemini"` node)
    /// so callers do not fall back to `AXValue` and accidentally ingest pseudo-element placeholder text.
    private static func surroundingTextViaTextMarkers(for element: AXUIElement) -> SurroundingText?? {
        guard let selectedMarkerRange = attribute("AXSelectedTextMarkerRange", of: element),
              let caretMarker = startTextMarker(for: selectedMarkerRange, in: element) else {
            return nil
        }

        // Inspect the specific DOM/AX node holding the caret. In rich-text editors like Quill
        // (`<div class="ql-editor ql-blank"><p><br></p></div>`), CSS `::before` placeholder text
        // lives on a separate sibling node while `caretMarker` points to the empty `<p>` / `<br>`.
        if let caretNodeVal = parameterizedAttribute("AXUIElementForTextMarker", parameter: caretMarker, of: element),
           CFGetTypeID(caretNodeVal) == AXUIElementGetTypeID() {
            let caretNode = caretNodeVal as! AXUIElement
            if !CFEqual(caretNode, element) {
                if let caretClasses = domClassList(for: caretNode),
                   isEmptyEditorClassList(caretClasses, includeLeafClasses: true) {
                    return .some(nil)
                }
                if let nodeRange = parameterizedAttribute("AXTextMarkerRangeForUIElement", parameter: caretNode, of: element),
                   let nodeText = stringForTextMarkerRange(in: element, markerRange: nodeRange) {
                    if isEmptyCaretContainerText(nodeText) {
                        return .some(nil)
                    }
                } else if let nodeValue = attribute(kAXValueAttribute as String, of: caretNode) as? String,
                          isEmptyCaretContainerText(nodeValue) {
                    return .some(nil)
                }
            }
        }

        // Resolve the current line's text and exact caret offset within that line via markers.
        guard let lineRange = parameterizedAttribute("AXLineTextMarkerRangeForTextMarker", parameter: caretMarker, of: element),
              let lineText = stringForTextMarkerRange(in: element, markerRange: lineRange) else {
            return nil
        }

        if isEmptyCaretContainerText(lineText) {
            return .some(nil)
        }

        guard let lineStartMarker = startTextMarker(for: lineRange, in: element) else {
            return nil
        }

        if CFEqual(lineStartMarker, caretMarker) {
            return .some(nil)
        }

        let orderedMarkers = [lineStartMarker, caretMarker] as CFArray
        guard let prefixRange = parameterizedAttribute(
            "AXTextMarkerRangeForUnorderedTextMarkers",
            parameter: orderedMarkers,
            of: element
        ),
              let prefixText = stringForTextMarkerRange(in: element, markerRange: prefixRange) else {
            return nil
        }

        if isEmptyCaretContainerText(prefixText) {
            return .some(nil)
        }

        return .some(
            surroundingText(
                in: lineText,
                textOffset: 0,
                caretLocation: prefixText.utf16.count
            )
        )
    }

    /// Retrieves the text surrounding the caret up to the word boundary from the currently focused element.
    static func getSurroundingText(for element: AXUIElement? = nil) -> SurroundingText? {
        guard let targetElement = element ?? getFocusedElement() else {
            return nil
        }
        AXUIElementSetMessagingTimeout(targetElement, messagingTimeoutSeconds)

        guard isEditableTextElement(targetElement) else {
            return nil
        }

        // Fast-path for rich-text web editors (Quill `.ql-blank`, TipTap `.is-editor-empty`, CKEditor
        // `.ck-placeholder`) whose `AXValue` contains CSS `::before` placeholder text when empty.
        if hasEmptyEditorDOMClass(targetElement) {
            return nil
        }

        let placeholderValue = attribute(kAXPlaceholderValueAttribute as String, of: targetElement) as? String

        // 0. For web/Electron `contenteditable` editors exposing `AXTextMarker` APIs, resolve
        // paragraph/line-scoped text directly so CSS `::before` placeholders never leak in.
        if let markerOutcome = surroundingTextViaTextMarkers(for: targetElement) {
            guard let resolved = markerOutcome else { return nil }
            if let placeholderValue,
               isPlaceholderMatch(
                   value: String(utf16CodeUnits: resolved.wordUTF16, count: resolved.wordUTF16.count),
                   placeholder: placeholderValue
               ) {
                return nil
            }
            return resolved
        }

        // Require a valid `kAXSelectedTextRangeAttribute` caret location; never guess `text.utf16.count`
        // when selection is missing or `NSNotFound` (e.g. after backspacing an empty `contenteditable`).
        guard let selectedRange = cfRange(from: attribute(kAXSelectedTextRangeAttribute as String, of: targetElement)),
              isValidCaretLocation(selectedRange.location) else {
            return nil
        }
        let location = selectedRange.location
        if location == 0 {
            return nil
        }

        var text: String?
        var textOffset = 0

        // 1. Fast path: Query a small clamped UTF-16 window directly via kAXStringForRangeParameterizedAttribute.
        // Works reliably in Apple Pages/Keynote/Numbers (even at end-of-storage where AXLineForIndex returns nil)
        // as well as native NSTextView, Xcode, Safari, and Chromium text areas.
        let windowRadius = 64
        let start = max(0, location - windowRadius)
        var maxEnd = location + windowRadius

        if let totalCount = attribute(kAXNumberOfCharactersAttribute as String, of: targetElement) as? Int {
            if totalCount <= 0 {
                return nil
            }
            maxEnd = min(maxEnd, totalCount)
        }

        let length = max(0, maxEnd - start)
        if length > 0 && start <= location {
            var windowRange = CFRange(location: start, length: length)
            if let windowRangeVal = AXValueCreate(.cfRange, &windowRange),
               let windowString = parameterizedAttribute(
                   kAXStringForRangeParameterizedAttribute as String,
                   parameter: windowRangeVal,
                   of: targetElement
               ) as? String {
                text = windowString
                textOffset = start
            }
        }

        // 2. Line-level fallback: kAXLineForIndexParameterizedAttribute -> kAXRangeForLineParameterizedAttribute -> kAXStringForRangeParameterizedAttribute
        if text == nil,
           let lineNumber = parameterizedAttribute(
               kAXLineForIndexParameterizedAttribute as String,
               parameter: location as CFNumber,
               of: targetElement
           ) as? Int,
           let lineRangeVal = parameterizedAttribute(
               kAXRangeForLineParameterizedAttribute as String,
               parameter: lineNumber as CFNumber,
               of: targetElement
           ),
           let lineRange = cfRange(from: lineRangeVal),
           lineRange.length > 0,
           let lineString = parameterizedAttribute(
               kAXStringForRangeParameterizedAttribute as String,
               parameter: lineRangeVal,
               of: targetElement
           ) as? String {
            text = lineString
            textOffset = lineRange.location
        }

        // 3. Fallback to full value (crucial for Chrome Omnibox & non-parameterized inputs)
        if text == nil,
           let fullString = attribute(kAXValueAttribute as String, of: targetElement) as? String {
            text = fullString
            textOffset = 0
        }

        guard let text, !text.isEmpty else { return nil }
        if textOffset == 0 && isPlaceholderMatch(value: text, placeholder: placeholderValue) {
            return nil
        }

        return surroundingText(in: text, textOffset: textOffset, caretLocation: location)
    }

    /// Computes the word boundary and surrounding text around a given caret location in a string.
    static func surroundingText(in text: String, textOffset: Int = 0, caretLocation: Int) -> SurroundingText {
        let utf16 = text.utf16
        let clampedCaretOffset = max(0, caretLocation - textOffset)
        let caretIndex = utf16.index(
            utf16.startIndex,
            offsetBy: clampedCaretOffset,
            limitedBy: utf16.endIndex
        ) ?? utf16.endIndex

        // Scan backwards from caretIndex for word boundary.
        var startIndex = caretIndex
        var caretOffsetInWord = 0
        while startIndex > utf16.startIndex {
            let prevIndex = utf16.index(before: startIndex)
            if okeytre_is_word_boundary(utf16[prevIndex]) {
                break
            }
            startIndex = prevIndex
            caretOffsetInWord += 1
        }

        // Scan forwards from caretIndex for word boundary.
        var endIndex = caretIndex
        while endIndex < utf16.endIndex {
            if okeytre_is_word_boundary(utf16[endIndex]) {
                break
            }
            utf16.formIndex(after: &endIndex)
        }

        return SurroundingText(
            wordUTF16: Array(utf16[startIndex..<endIndex]),
            caretOffset: caretOffsetInWord
        )
    }
}
