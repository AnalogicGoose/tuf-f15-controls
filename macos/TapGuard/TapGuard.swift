// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// TapGuard: "ignore trackpad taps while typing" for the GA403WM's I2C touchpad.
// VoodooInput only gets keyboard activity from VoodooPS2Keyboard; this machine's keyboard is USB (0b05:19b6), so its own
// typing quiet time never triggers. TapGuard does it in user space: a session event tap notes every key press and drops a
// mouse button press (and its matching release) that comes within QuietMs of the last key press. Shortcuts held with
// Command or Control do not count as typing (Cmd+click keeps working). Needs Accessibility permission (System Settings >
// Privacy & Security > Accessibility > TapGuard); it waits until granted.
//   defaults write com.itlmt7925.TapGuard QuietMs -int 350     (100..1500, default 350)
//   defaults write com.itlmt7925.TapGuard Enabled -bool false  (turn it off without uninstalling)
// Build / install: scripts/build-tapguard.sh [--install]
import AppKit
import ApplicationServices

let defaults = UserDefaults(suiteName: "com.itlmt7925.TapGuard") ?? .standard
var quiet: Double { let v = defaults.integer(forKey: "QuietMs"); return Double(v >= 100 && v <= 1500 ? v : 350) / 1000 }
var enabled: Bool { defaults.object(forKey: "Enabled") as? Bool ?? true }

var lastKey: CFAbsoluteTime = 0
var swallowed = Set<Int64>()          // buttons whose press was dropped: drop their release too
var dropped = 0
var tap: CFMachPort?

func callback(_ proxy: CGEventTapProxy, _ type: CGEventType, _ ev: CGEvent, _ ctx: UnsafeMutableRawPointer?) -> Unmanaged<CGEvent>? {
    switch type {
    case .tapDisabledByTimeout, .tapDisabledByUserInput:
        if let t = tap { CGEvent.tapEnable(tap: t, enable: true) }
        return Unmanaged.passUnretained(ev)
    case .keyDown:
        if ev.flags.intersection([.maskCommand, .maskControl]).isEmpty && ev.getIntegerValueField(.keyboardEventAutorepeat) == 0 {
            lastKey = CFAbsoluteTimeGetCurrent()
        }
    case .leftMouseDown, .rightMouseDown, .otherMouseDown:
        let b = ev.getIntegerValueField(.mouseEventButtonNumber)
        if enabled && CFAbsoluteTimeGetCurrent() - lastKey < quiet && ev.flags.intersection([.maskCommand, .maskControl]).isEmpty {
            swallowed.insert(b); dropped += 1
            return nil
        }
    case .leftMouseUp, .rightMouseUp, .otherMouseUp:
        let b = ev.getIntegerValueField(.mouseEventButtonNumber)
        if swallowed.remove(b) != nil { return nil }
    default: break
    }
    return Unmanaged.passUnretained(ev)
}

func install() -> Bool {
    let types: [CGEventType] = [.keyDown, .leftMouseDown, .leftMouseUp, .rightMouseDown, .rightMouseUp, .otherMouseDown, .otherMouseUp]
    let mask = types.reduce(CGEventMask(0)) { $0 | (CGEventMask(1) << $1.rawValue) }
    guard let t = CGEvent.tapCreate(tap: .cgSessionEventTap, place: .headInsertEventTap, options: .defaultTap,
                                    eventsOfInterest: mask, callback: callback, userInfo: nil) else { return false }
    tap = t
    CFRunLoopAddSource(CFRunLoopGetMain(), CFMachPortCreateRunLoopSource(nil, t, 0), .commonModes)
    CGEvent.tapEnable(tap: t, enable: true)
    return true
}

// Ask once (shows the system prompt), then poll until the user has granted Accessibility.
_ = AXIsProcessTrustedWithOptions([kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary)
func tryStart() {
    if AXIsProcessTrusted() && install() {
        NSLog("TapGuard: active, quiet time %.0f ms", quiet * 1000)
        Timer.scheduledTimer(withTimeInterval: 600, repeats: true) { _ in if dropped > 0 { NSLog("TapGuard: %d taps ignored", dropped) } }
        return
    }
    DispatchQueue.main.asyncAfter(deadline: .now() + 3) { tryStart() }
}
tryStart()
NSApplication.shared.setActivationPolicy(.prohibited)
NSApplication.shared.run()
