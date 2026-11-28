// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import CoreGraphics

enum KeyCodes {
    static let backspaceKeyCode: CGKeyCode = 51
    static let leftCommandKeyCode: CGKeyCode = 55
    static let rightCommandKeyCode: CGKeyCode = 54
    static let leftShiftKeyCode: CGKeyCode = 56
    static let rightShiftKeyCode: CGKeyCode = 60
}

extension CGEventFlags {
    nonisolated static let disruptingModifiers: CGEventFlags = [
        .maskCommand, .maskControl, .maskAlternate,
    ]
}
