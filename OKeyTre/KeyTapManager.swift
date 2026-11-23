// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import AppKit
import ApplicationServices
import CoreFoundation
import CoreGraphics
import Foundation
import os

private nonisolated final class TapPortBox: @unchecked Sendable {
    private struct State {
        var eventTap: CFMachPort?
        var runLoopSource: CFRunLoopSource?
    }

    private let lock = OSAllocatedUnfairLock(initialState: State())

    var isInstalled: Bool {
        lock.withLock { state in
            guard let tap = state.eventTap else { return false }
            return CFMachPortIsValid(tap)
        }
    }

    func install(tap: CFMachPort, source: CFRunLoopSource) {
        lock.withLock { state in
            state.eventTap = tap
            state.runLoopSource = source
        }
    }

    func reenableIfPresent() {
        lock.withLock { state in
            if let tap = state.eventTap, CFMachPortIsValid(tap) {
                CGEvent.tapEnable(tap: tap, enable: true)
            }
        }
    }

    @discardableResult
    func tearDown() -> Bool {
        let (tap, source) = lock.withLock { state -> (CFMachPort?, CFRunLoopSource?) in
            let current = (state.eventTap, state.runLoopSource)
            state.eventTap = nil
            state.runLoopSource = nil
            return current
        }
        guard tap != nil || source != nil else { return false }
        if let tap {
            CGEvent.tapEnable(tap: tap, enable: false)
        }
        if let source {
            CFRunLoopSourceInvalidate(source)
            CFRunLoopRemoveSource(CFRunLoopGetMain(), source, .commonModes)
        }
        if let tap {
            CFMachPortInvalidate(tap)
        }
        return true
    }
}

struct DualShiftDetector {
    private(set) var isLeftShiftPressed = false
    private(set) var isRightShiftPressed = false

    mutating func reset() {
        isLeftShiftPressed = false
        isRightShiftPressed = false
    }

    mutating func shouldTriggerRestore(keyCode: CGKeyCode, flags: CGEventFlags) -> Bool {
        guard flags.contains(.maskShift) else {
            reset()
            return false
        }

        switch keyCode {
        case KeyCodes.leftShiftKeyCode:
            isLeftShiftPressed.toggle()
        case KeyCodes.rightShiftKeyCode:
            isRightShiftPressed.toggle()
        default:
            return false
        }

        let disruptingModifiers: CGEventFlags = [.maskCommand, .maskControl, .maskAlternate]
        guard flags.intersection(disruptingModifiers).isEmpty else {
            return false
        }

        return isLeftShiftPressed && isRightShiftPressed
    }
}

class KeyTapManager {
    nonisolated static let shared = KeyTapManager()

    private let magicTag: Int64 = 0xCAFE_BABE
    private nonisolated let portBox = TapPortBox()
    private var mouseMonitor: Any?
    private var dualShiftDetector = DualShiftDetector()
    private var inputMethodBuffer: InputMethodBuffer = {
        let defaults = UserDefaults.standard
        let style = defaults.string(forKey: InputMethodBuffer.inputMethodDefaultsKey)
            .flatMap(InputMethodStyle.init(rawValue:)) ?? .telex
        let storedMaxSize = defaults.integer(forKey: InputMethodBuffer.maxBufferSizeDefaultsKey)
        return InputMethodBuffer(
            style: style,
            modernTone: InputMethodBuffer.resolvedModernTone(from: defaults),
            uoHorn: defaults.bool(forKey: InputMethodBuffer.uoHornDefaultsKey),
            ngToNgh: defaults.bool(forKey: InputMethodBuffer.ngToNghDefaultsKey),
            nghToNg: defaults.bool(forKey: InputMethodBuffer.nghToNgDefaultsKey),
            maxBufferSize: storedMaxSize > 0 ? storedMaxSize : InputMethodBuffer.defaultMaxBufferSize
        )
    }()

    /// Currently active input method style (`.telex`, `.vni`, or `.viqr`).
    var inputMethodStyle: InputMethodStyle {
        get { inputMethodBuffer.style }
        set { inputMethodBuffer.setStyle(newValue) }
    }

    var modernTone: Bool {
        get { inputMethodBuffer.modernTone }
        set { inputMethodBuffer.modernTone = newValue }
    }

    var uoHorn: Bool {
        get { inputMethodBuffer.uoHorn }
        set { inputMethodBuffer.uoHorn = newValue }
    }

    var ngToNgh: Bool {
        get { inputMethodBuffer.ngToNgh }
        set { inputMethodBuffer.ngToNgh = newValue }
    }

    var nghToNg: Bool {
        get { inputMethodBuffer.nghToNg }
        set { inputMethodBuffer.nghToNg = newValue }
    }

    var maxBufferSize: Int {
        get { inputMethodBuffer.maxBufferSize }
        set { inputMethodBuffer.maxBufferSize = newValue }
    }

    private init() {
        let workspaceCenter = NSWorkspace.shared.notificationCenter
        workspaceCenter.addObserver(
            forName: NSWorkspace.didActivateApplicationNotification,
            object: nil,
            queue: .main
        ) { [weak self] notification in
            self?.dualShiftDetector.reset()
            self?.inputMethodBuffer.reset()
            guard PermissionManager.shared.verifyPermissionNow() else { return }
            if let app = notification.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication {
                CaretTextReader.enableEnhancedUserInterface(for: app.processIdentifier)
            }
        }
        workspaceCenter.addObserver(
            forName: NSWorkspace.didTerminateApplicationNotification,
            object: nil,
            queue: .main
        ) { notification in
            if let app = notification.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication {
                CaretTextReader.handleAppTerminated(pid: app.processIdentifier)
            }
        }
    }

    @discardableResult
    nonisolated func tearDownTapPortImmediately() -> Bool {
        portBox.tearDown()
    }

    @discardableResult
    func start() -> Bool {
        if portBox.isInstalled {
            return true
        }

        let eventMask = (1 << CGEventType.keyDown.rawValue) |
                        (1 << CGEventType.flagsChanged.rawValue)

        guard let tap = CGEvent.tapCreate(
            tap: .cgSessionEventTap,
            place: .headInsertEventTap,
            options: .defaultTap,
            eventsOfInterest: CGEventMask(eventMask),
            callback: { (proxy, type, event, _) -> Unmanaged<CGEvent>? in
                MainActor.assumeIsolated {
                    KeyTapManager.shared.handleEvent(proxy: proxy, type: type, event: event)
                }
            },
            userInfo: nil
        ), let source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0) else {
            print("Failed to create Event Tap. Double check Accessibility permissions.")
            return false
        }

        CFRunLoopAddSource(CFRunLoopGetMain(), source, .commonModes)
        CGEvent.tapEnable(tap: tap, enable: true)
        portBox.install(tap: tap, source: source)
        print("Event Tap successfully initialized")

        if mouseMonitor == nil {
            mouseMonitor = NSEvent.addGlobalMonitorForEvents(
                matching: [.leftMouseDown, .rightMouseDown, .otherMouseDown]
            ) { [weak self] _ in
                self?.dualShiftDetector.reset()
                self?.inputMethodBuffer.reset()
            }
        }

        if let frontApp = NSWorkspace.shared.frontmostApplication {
            CaretTextReader.enableEnhancedUserInterface(for: frontApp.processIdentifier)
        }
        return true
    }

    func stop() {
        let hadTap = portBox.tearDown()
        if let monitor = mouseMonitor {
            NSEvent.removeMonitor(monitor)
            mouseMonitor = nil
        }
        dualShiftDetector.reset()
        inputMethodBuffer.reset()
        if hadTap {
            print("Event Tap turned off")
        }
    }

    private func handleEvent(proxy: CGEventTapProxy, type: CGEventType, event: CGEvent) -> Unmanaged<CGEvent>? {
        if type == .tapDisabledByUserInput || type == .tapDisabledByTimeout {
            dualShiftDetector.reset()
            inputMethodBuffer.reset()
            // Verify live authorization via `WindowServer` + cross-process AX IPC rather than
            // `AXIsProcessTrusted()`'s stale cache before ever re-enabling a disabled tap.
            if PermissionManager.shared.verifyPermissionNow() {
                portBox.reenableIfPresent()
            }
            return nil
        }

        guard portBox.isInstalled, type == .keyDown || type == .flagsChanged else {
            return Unmanaged.passUnretained(event)
        }

        if event.getIntegerValueField(.eventSourceUserData) == magicTag {
            return Unmanaged.passUnretained(event)
        }

        let flags = event.flags

        if type == .flagsChanged {
            let keyCode = CGKeyCode(event.getIntegerValueField(.keyboardEventKeycode))
            if dualShiftDetector.shouldTriggerRestore(keyCode: keyCode, flags: flags),
               let action = inputMethodBuffer.restoreCurrentWord() {
                postAction(action, proxy: proxy)
            }
            return Unmanaged.passUnretained(event)
        }

        if !flags.contains(.maskShift) {
            dualShiftDetector.reset()
        }

        // If Command (⌘), Control (⌃), or Option (⌥) is held down (e.g. Cmd+A, Cmd+C, Cmd+V,
        // Ctrl+A, Option+Left), reset the buffer and pass the event through untouched without
        // querying AXUIElement or combining with the existing buffer.
        let disruptingModifiers: CGEventFlags = [.maskCommand, .maskControl, .maskAlternate]
        if !flags.intersection(disruptingModifiers).isEmpty {
            dualShiftDetector.reset()
            inputMethodBuffer.reset()
            return Unmanaged.passUnretained(event)
        }

        let action = withUnsafeTemporaryAllocation(of: UniChar.self, capacity: 4) { buf in
            var actualLength = 0
            event.keyboardGetUnicodeString(
                maxStringLength: buf.count,
                actualStringLength: &actualLength,
                unicodeString: buf.baseAddress
            )
            let count = min(max(0, actualLength), buf.count)
            return inputMethodBuffer.handleInput(
                UnsafeBufferPointer(start: buf.baseAddress, count: count),
                flags: flags
            )
        }
        if let action {
            postAction(action, proxy: proxy)
            return nil
        }

        return Unmanaged.passUnretained(event)
    }

    private func postAction(_ action: InputMethodBuffer.Action, proxy: CGEventTapProxy) {
        let source = CGEventSource(stateID: .combinedSessionState)
        // If text is currently selected (e.g. browser URL bar inline auto-completion), an extra
        // Backspace clears the selected range before deleting `action.backspaceCount` code units.
        let totalBackspaces = action.backspaceCount + (CaretTextReader.getSelectedRangeLength() > 0 ? 1 : 0)
        for _ in 0..<totalBackspaces {
            sendTaggedVirtualKey(keyCode: KeyCodes.backspaceKeyCode, source: source, proxy: proxy)
        }
        if !action.text.isEmpty {
            sendTaggedUnicodeString(action.text, source: source, proxy: proxy)
        }
    }

    private func sendTaggedVirtualKey(
        keyCode: CGKeyCode,
        source: CGEventSource?,
        proxy: CGEventTapProxy
    ) {
        guard let keyDown = CGEvent(keyboardEventSource: source, virtualKey: keyCode, keyDown: true),
              let keyUp = CGEvent(keyboardEventSource: source, virtualKey: keyCode, keyDown: false) else { return }

        keyDown.setIntegerValueField(.eventSourceUserData, value: magicTag)
        keyUp.setIntegerValueField(.eventSourceUserData, value: magicTag)

        keyDown.tapPostEvent(proxy)
        keyUp.tapPostEvent(proxy)
    }

    private func sendTaggedUnicodeString(
        _ string: String,
        source: CGEventSource?,
        proxy: CGEventTapProxy
    ) {
        guard let event = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: true) else { return }

        event.setIntegerValueField(.eventSourceUserData, value: magicTag)
        let chars = Array(string.utf16)
        event.keyboardSetUnicodeString(stringLength: chars.count, unicodeString: chars)

        event.tapPostEvent(proxy)
    }
}
