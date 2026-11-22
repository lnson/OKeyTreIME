// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import Foundation
import ApplicationServices

enum CaretTextReader {
    struct SurroundingText {
        let wordUTF16: [UniChar]
        let caretOffset: Int
    }
    
    /// Shared system-wide accessibility element with a 0.1s global messaging timeout for this process.
    private static let systemWide: AXUIElement = {
        let element = AXUIElementCreateSystemWide()
        AXUIElementSetMessagingTimeout(element, 0.1)
        return element
    }()

    /// Tracks PIDs for which we have already enabled `AXEnhancedUserInterface` / `AXManualAccessibility`.
    private static var enhancedPids = Set<pid_t>()
    
    /// Enables `AXManualAccessibility` (for Electron apps like VS Code) and `AXEnhancedUserInterface`
    /// (for Chromium browsers and Apple iWork apps like Pages/Keynote/Numbers) once per process.
    static func enableEnhancedUserInterface(for pid: pid_t) {
        _ = systemWide
        guard pid > 0,
              pid != ProcessInfo.processInfo.processIdentifier,
              !enhancedPids.contains(pid) else { return }

        let appElement = AXUIElementCreateApplication(pid)
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

    /// Resolves the currently focused UI element, including fallbacks for Apple Pages/iWork and Chromium-based applications.
    static func getFocusedElement() -> AXUIElement? {
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
            AXValueGetValue(rangeVal as! AXValue, .cfRange, &selectedRange)
            if selectedRange.length > 0 {
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
    
    /// Retrieves the text surrounding the caret up to the word boundary from the currently focused element.
    static func getSurroundingText(for element: AXUIElement? = nil) -> SurroundingText? {
        guard let targetElement = element ?? getFocusedElement() else {
            return nil
        }
        
        var caretLocation: Int? = nil
        
        // Check selected text range for caret location
        var rangeRef: CFTypeRef?
        if AXUIElementCopyAttributeValue(targetElement, kAXSelectedTextRangeAttribute as CFString, &rangeRef) == .success,
           let rangeVal = rangeRef, CFGetTypeID(rangeVal) == AXValueGetTypeID() {
            var selectedRange = CFRange()
            AXValueGetValue(rangeVal as! AXValue, .cfRange, &selectedRange)
            caretLocation = selectedRange.location
        }
        
        var text: String?
        var textOffset = 0
        
        if let location = caretLocation {
            // 1. Fast path: Query a small clamped UTF-16 window directly via kAXStringForRangeParameterizedAttribute.
            // Works reliably in Apple Pages/Keynote/Numbers (even at end-of-storage where AXLineForIndex returns nil)
            // as well as native NSTextView, Xcode, Safari, and Chromium text areas.
            let windowRadius = 64
            let start = max(0, location - windowRadius)
            var maxEnd = location + windowRadius
            
            var countRef: CFTypeRef?
            if AXUIElementCopyAttributeValue(targetElement, kAXNumberOfCharactersAttribute as CFString, &countRef) == .success,
               let totalCount = countRef as? Int {
                maxEnd = min(maxEnd, totalCount)
            }
            
            let length = max(0, maxEnd - start)
            if start <= location {
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
                        if AXUIElementCopyParameterizedAttributeValue(
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
        
        guard let text = text else { return nil }
        
        // Default caretLocation to end of text if range wasn't readable
        let finalCaretLocation = caretLocation ?? text.utf16.count
        return surroundingText(in: text, textOffset: textOffset, caretLocation: finalCaretLocation)
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
