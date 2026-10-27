// SPDX-License-Identifier: BSD-3-Clause-Clear
// FX517ZM HiDPI helper for the scale-resolutions override in
// /Library/Displays/Contents/Resources/Overrides/DisplayVendorID-dae/DisplayProductID-153c.
// Unlike tools/hidpi-try.swift (G14), this allows scaled HiDPI (desktop pixels != panel pixels): on this TUF with
// NullMoth 2.5.5 an app-only switch to the scaled 1344x756 mode showed a picture (2026-10-26), it did not go black.
// Build: swiftc -O tools/hidpi-pick.swift -o /tmp/hidpi-pick
// Run:   hidpi-pick list            usable desktop modes of the built-in display, HiDPI ones marked
//        hidpi-pick test 1680       app-only switch to "looks like 1680 x ..." HiDPI for 10 s; WindowServer restores the
//                                   previous mode when the process exits, even on a black screen
//        hidpi-pick set 1680        make it the saved mode; reverts by itself unless Return is pressed within 15 s
//        hidpi-pick set native      back to 1920x1080
import CoreGraphics
import Foundation

let display = CGMainDisplayID()
let options = [kCGDisplayShowDuplicateLowResolutionModes: true] as CFDictionary
let modes = ((CGDisplayCopyAllDisplayModes(display, options) as? [CGDisplayMode]) ?? []).filter { $0.isUsableForDesktopGUI() }
func describe(_ m: CGDisplayMode) -> String {
    String(format: "looks like %4dx%-4d  pixels %4dx%-4d  %3.0f Hz%@", m.width, m.height, m.pixelWidth, m.pixelHeight, m.refreshRate,
           m.pixelWidth > m.width ? "  HiDPI" : "")
}
func pick(_ arg: String) -> CGDisplayMode? {
    let candidates = arg == "native"
        ? modes.filter { $0.width == 1920 && $0.pixelWidth == 1920 }
        : modes.filter { $0.width == Int(arg) && $0.pixelWidth == 2 * $0.width }
    return candidates.max { $0.refreshRate < $1.refreshRate }
}
func apply(_ m: CGDisplayMode, _ option: CGConfigureOption) -> CGError {
    var cfg: CGDisplayConfigRef?
    CGBeginDisplayConfiguration(&cfg)
    CGConfigureDisplayWithDisplayMode(cfg, display, m, nil)
    return CGCompleteDisplayConfiguration(cfg, option)
}

let args = CommandLine.arguments
guard args.count >= 2 else { print("usage: hidpi-pick list | test <width> | set <width|native>"); exit(2) }
switch args[1] {
case "list":
    if let c = CGDisplayCopyDisplayMode(display) { print("current: " + describe(c)) }
    for m in modes { print("  " + describe(m)) }
case "test" where args.count >= 3:
    guard let m = pick(args[2]) else { print("no usable HiDPI mode looks like \(args[2]) wide (reboot after installing the override?)"); exit(1) }
    print("testing " + describe(m) + " for 10 s (this app only)"); fflush(stdout)
    print("result \(apply(m, .forAppOnly).rawValue)"); fflush(stdout)
    sleep(10)
    CGRestorePermanentDisplayConfiguration()
    print("restored")
case "set" where args.count >= 3:
    guard let m = pick(args[2]), let previous = CGDisplayCopyDisplayMode(display) else { print("no usable mode for \(args[2])"); exit(1) }
    print("setting " + describe(m) + ". Press Return within 15 s to keep it."); fflush(stdout)
    guard apply(m, .permanently) == .success else { print("switch failed"); exit(1) }
    var kept = false
    let done = DispatchSemaphore(value: 0)
    DispatchQueue.global().async { if readLine() != nil { kept = true }; done.signal() }
    _ = done.wait(timeout: .now() + 15)
    if kept { print("kept") } else { _ = apply(previous, .permanently); print("no answer: reverted to " + describe(previous)) }
default:
    print("usage: hidpi-pick list | test <width> | set <width|native>"); exit(2)
}
