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
        var rangeRef: CFTypeRef?
        if AXUIElementCopyAttributeValue(element, kAXSelectedTextRangeAttribute as CFString, &rangeRef) == .success,
           let val = rangeRef, CFGetTypeID(val) == AXValueGetTypeID() {
            return true
        }
        return false
    }

    private static let isAutomatedEnvironment: Bool = {
        let env = ProcessInfo.processInfo.environment
        return env["XCODE_RUNNING_FOR_PREVIEWS"] == "1"
            || env["XCTestConfigurationFilePath"] != nil
            || env["XCTestBundlePath"] != nil
            || env["XCTestSessionIdentifier"] != nil
    }()

    /// Resolves the currently focused UI element, including fallbacks for Apple Pages/iWork and Chromium-based applications.
    static func getFocusedElement() -> AXUIElement? {
        guard !isAutomatedEnvironment else { return nil }
        var focusedRef: CFTypeRef?
        if AXUIElementCopyAttributeValue(systemWide, kAXFocusedUIElementAttribute as CFString, &focusedRef) == .success,
           let focusedVal = focusedRef {
            let focused = focusedVal as! AXUIElement
            var pid: pid_t = 0
            if AXUIElementGetPid(focused, &pid) == .success, pid > 0, !enhancedPids.contains(pid) {
                // Apps like Apple Pages return the raw canvas view until AXEnhancedUserInterface is enabled.
                if !supportsTextSelection(focused) {
                    enableEnhancedUserInterface(for: pid)
                    let appElement = AXUIElementCreateApplication(pid)
                    var refreshedRef: CFTypeRef?
                    if AXUIElementCopyAttributeValue(appElement, kAXFocusedUIElementAttribute as CFString, &refreshedRef) == .success,
                       let refreshed = refreshedRef {
                        return (refreshed as! AXUIElement)
                    }
                    if AXUIElementCopyAttributeValue(systemWide, kAXFocusedUIElementAttribute as CFString, &refreshedRef) == .success,
                       let refreshed = refreshedRef {
                        return (refreshed as! AXUIElement)
                    }
                }
            }
            return focused
        }
        
        // Fallback: Query systemWide focused application, then query its focused element
        var focusedAppRef: CFTypeRef?
        if AXUIElementCopyAttributeValue(systemWide, kAXFocusedApplicationAttribute as CFString, &focusedAppRef) == .success,
           let appVal = focusedAppRef {
            let focusedApp = appVal as! AXUIElement
            var pid: pid_t = 0
            if AXUIElementGetPid(focusedApp, &pid) == .success, pid > 0 {
                enableEnhancedUserInterface(for: pid)
            }
            
            var appFocusedRef: CFTypeRef?
            if AXUIElementCopyAttributeValue(focusedApp, kAXFocusedUIElementAttribute as CFString, &appFocusedRef) == .success,
               let appFocused = appFocusedRef {
                return (appFocused as! AXUIElement)
            }
        }
        return nil
    }
    
    /// Returns the length of the currently selected text in the focused element, or 0 if no selection.
    static func getSelectedRangeLength(for element: AXUIElement? = nil) -> Int {
        guard let targetElement = element ?? getFocusedElement() else {
            return 0
        }
        
        // 1. Primary check: kAXSelectedTextRangeAttribute
        var rangeRef: CFTypeRef?
        if AXUIElementCopyAttributeValue(targetElement, kAXSelectedTextRangeAttribute as CFString, &rangeRef) == .success,
           let rangeVal = rangeRef, CFGetTypeID(rangeVal) == AXValueGetTypeID() {
            var selectedRange = CFRange()
            if AXValueGetValue(rangeVal as! AXValue, .cfRange, &selectedRange),
               isValidCaretLocation(selectedRange.location),
               selectedRange.length > 0,
               selectedRange.length < (CFIndex.max - 1024) {
                return selectedRange.length
            }
        }
        
        // 2. Secondary check: kAXSelectedTextAttribute (e.g. Chrome Omnibox inline autocomplete)
        var selectedTextRef: CFTypeRef?
        if AXUIElementCopyAttributeValue(targetElement, kAXSelectedTextAttribute as CFString, &selectedTextRef) == .success,
           let str = selectedTextRef as? String, !str.isEmpty {
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
        var classListRef: CFTypeRef?
        guard AXUIElementCopyAttributeValue(
            element,
            "AXDOMClassList" as CFString,
            &classListRef
        ) == .success else {
            return nil
        }
        return classListRef as? [String]
    }

    private static func hasEmptyEditorDOMClass(_ element: AXUIElement) -> Bool {
        var classListRef: CFTypeRef?
        let err = AXUIElementCopyAttributeValue(
            element,
            "AXDOMClassList" as CFString,
            &classListRef
        )
        if err == .attributeUnsupported {
            return false
        }
        if err == .success,
           let classes = classListRef as? [String],
           isEmptyEditorClassList(classes) {
            return true
        }
        // Also check the immediate parent in case focus sits on an unclassed `<p>` inside `.ql-editor.ql-blank`.
        var parentRef: CFTypeRef?
        if AXUIElementCopyAttributeValue(
            element,
            kAXParentAttribute as CFString,
            &parentRef
        ) == .success,
           let parentVal = parentRef,
           CFGetTypeID(parentVal) == AXUIElementGetTypeID(),
           let parentClasses = domClassList(for: parentVal as! AXUIElement),
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
        var roleRef: CFTypeRef?
        if AXUIElementCopyAttributeValue(element, kAXRoleAttribute as CFString, &roleRef) == .success,
           let role = roleRef as? String,
           editableRoles.contains(role) {
            return true
        }

        var editableRef: CFTypeRef?
        if AXUIElementCopyAttributeValue(element, "AXEditable" as CFString, &editableRef) == .success,
           let editable = editableRef as? Bool,
           editable {
            return true
        }

        var isSettable: DarwinBoolean = false
        if AXUIElementIsAttributeSettable(element, kAXValueAttribute as CFString, &isSettable) == .success,
           isSettable.boolValue {
            return true
        }
        if AXUIElementIsAttributeSettable(element, kAXSelectedTextRangeAttribute as CFString, &isSettable) == .success,
           isSettable.boolValue {
            return true
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
        if let element {
            var startMarkerRef: CFTypeRef?
            if AXUIElementCopyParameterizedAttributeValue(
                element,
                "AXStartTextMarkerForTextMarkerRange" as CFString,
                markerRange,
                &startMarkerRef
            ) == .success, let startMarker = startMarkerRef {
                return startMarker
            }
        }
        return nil
    }

    private static func stringForTextMarkerRange(
        in element: AXUIElement,
        markerRange: CFTypeRef
    ) -> String? {
        var strRef: CFTypeRef?
        if AXUIElementCopyParameterizedAttributeValue(
            element,
            "AXStringForTextMarkerRange" as CFString,
            markerRange,
            &strRef
        ) == .success, let str = strRef as? String {
            return str
        }
        return nil
    }

    /// Attempts to resolve surrounding text via WebKit / Chromium `AXTextMarker` APIs on `contenteditable`
    /// elements. Returns `.some(nil)` when the caret sits inside an empty paragraph/placeholder leaf
    /// (such as Quill's `.ql-blank` `<p><br></p>` sibling next to a CSS `::before` `"Ask Gemini"` node)
    /// so callers do not fall back to `AXValue` and accidentally ingest pseudo-element placeholder text.
    private static func surroundingTextViaTextMarkers(for element: AXUIElement) -> SurroundingText?? {
        var selectedMarkerRangeRef: CFTypeRef?
        guard AXUIElementCopyAttributeValue(
            element,
            "AXSelectedTextMarkerRange" as CFString,
            &selectedMarkerRangeRef
        ) == .success, let selectedMarkerRange = selectedMarkerRangeRef else {
            return nil
        }

        guard let caretMarker = startTextMarker(for: selectedMarkerRange, in: element) else {
            return nil
        }

        // Inspect the specific DOM/AX node holding the caret. In rich-text editors like Quill
        // (`<div class="ql-editor ql-blank"><p><br></p></div>`), CSS `::before` placeholder text
        // lives on a separate sibling node while `caretMarker` points to the empty `<p>` / `<br>`.
        var caretNodeRef: CFTypeRef?
        if AXUIElementCopyParameterizedAttributeValue(
            element,
            "AXUIElementForTextMarker" as CFString,
            caretMarker,
            &caretNodeRef
        ) == .success, let caretNodeVal = caretNodeRef,
           CFGetTypeID(caretNodeVal) == AXUIElementGetTypeID() {
            let caretNode = caretNodeVal as! AXUIElement
            if !CFEqual(caretNode, element) {
                if let caretClasses = domClassList(for: caretNode),
                   isEmptyEditorClassList(caretClasses, includeLeafClasses: true) {
                    return .some(nil)
                }
                var nodeRangeRef: CFTypeRef?
                if AXUIElementCopyParameterizedAttributeValue(
                    element,
                    "AXTextMarkerRangeForUIElement" as CFString,
                    caretNode,
                    &nodeRangeRef
                ) == .success,
                   let nodeRange = nodeRangeRef,
                   let nodeText = stringForTextMarkerRange(in: element, markerRange: nodeRange) {
                    if isEmptyCaretContainerText(nodeText) {
                        return .some(nil)
                    }
                } else {
                    var nodeValueRef: CFTypeRef?
                    if AXUIElementCopyAttributeValue(
                        caretNode,
                        kAXValueAttribute as CFString,
                        &nodeValueRef
                    ) == .success,
                       let nodeValue = nodeValueRef as? String,
                       isEmptyCaretContainerText(nodeValue) {
                        return .some(nil)
                    }
                }
            }
        }

        // Resolve the current line's text and exact caret offset within that line via markers.
        var lineRangeRef: CFTypeRef?
        guard AXUIElementCopyParameterizedAttributeValue(
            element,
            "AXLineTextMarkerRangeForTextMarker" as CFString,
            caretMarker,
            &lineRangeRef
        ) == .success,
              let lineRange = lineRangeRef,
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
        var prefixRangeRef: CFTypeRef?
        guard AXUIElementCopyParameterizedAttributeValue(
            element,
            "AXTextMarkerRangeForUnorderedTextMarkers" as CFString,
            orderedMarkers,
            &prefixRangeRef
        ) == .success,
              let prefixRange = prefixRangeRef,
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

        var placeholderRef: CFTypeRef?
        let placeholderValue: String? = if AXUIElementCopyAttributeValue(
            targetElement,
            kAXPlaceholderValueAttribute as CFString,
            &placeholderRef
        ) == .success {
            placeholderRef as? String
        } else {
            nil
        }

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
        var rangeRef: CFTypeRef?
        guard AXUIElementCopyAttributeValue(
            targetElement,
            kAXSelectedTextRangeAttribute as CFString,
            &rangeRef
        ) == .success,
              let rangeVal = rangeRef,
              CFGetTypeID(rangeVal) == AXValueGetTypeID() else {
            return nil
        }

        var selectedRange = CFRange()
        guard AXValueGetValue(rangeVal as! AXValue, .cfRange, &selectedRange),
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
        
        var countRef: CFTypeRef?
        if AXUIElementCopyAttributeValue(targetElement, kAXNumberOfCharactersAttribute as CFString, &countRef) == .success,
           let totalCount = countRef as? Int {
            if totalCount <= 0 {
                return nil
            }
            maxEnd = min(maxEnd, totalCount)
        }
        
        let length = max(0, maxEnd - start)
        if length > 0 && start <= location {
            var windowRange = CFRange(location: start, length: length)
            if let windowRangeVal = AXValueCreate(.cfRange, &windowRange) {
                var stringRef: CFTypeRef?
                if AXUIElementCopyParameterizedAttributeValue(
                    targetElement,
                    kAXStringForRangeParameterizedAttribute as CFString,
                    windowRangeVal,
                    &stringRef
                ) == .success, let windowString = stringRef as? String {
                    text = windowString
                    textOffset = start
                }
            }
        }
        
        // 2. Line-level fallback: kAXLineForIndexParameterizedAttribute -> kAXRangeForLineParameterizedAttribute -> kAXStringForRangeParameterizedAttribute
        if text == nil {
            var lineIndexRef: CFTypeRef?
            if AXUIElementCopyParameterizedAttributeValue(
                targetElement,
                kAXLineForIndexParameterizedAttribute as CFString,
                location as CFNumber,
                &lineIndexRef
            ) == .success, let lineNumber = lineIndexRef as? Int {
                var lineRangeRef: CFTypeRef?
                if AXUIElementCopyParameterizedAttributeValue(
                    targetElement,
                    kAXRangeForLineParameterizedAttribute as CFString,
                    lineNumber as CFNumber,
                    &lineRangeRef
                ) == .success, let lineRangeVal = lineRangeRef, CFGetTypeID(lineRangeVal) == AXValueGetTypeID() {
                    var lineRange = CFRange()
                    AXValueGetValue(lineRangeVal as! AXValue, .cfRange, &lineRange)
                    var stringRef: CFTypeRef?
                    if lineRange.length > 0,
                       AXUIElementCopyParameterizedAttributeValue(
                        targetElement,
                        kAXStringForRangeParameterizedAttribute as CFString,
                        lineRangeVal,
                        &stringRef
                    ) == .success, let lineString = stringRef as? String {
                        text = lineString
                        textOffset = lineRange.location
                    }
                }
            }
        }
        
        // 3. Fallback to full value (crucial for Chrome Omnibox & non-parameterized inputs)
        if text == nil {
            var fullValueRef: CFTypeRef?
            if AXUIElementCopyAttributeValue(targetElement, kAXValueAttribute as CFString, &fullValueRef) == .success,
               let fullString = fullValueRef as? String {
                text = fullString
                textOffset = 0
            }
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
