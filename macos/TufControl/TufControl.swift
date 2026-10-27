// SPDX-License-Identifier: BSD-3-Clause-Clear
// FX517ZM adaptation of the RogControl UI/WMNB design by thanhnndev and contributors.
// TufControl: menu bar popover and app window for the ASUS TUF Dash F15 (FX517ZM): performance modes (Silent,
// Performance, Turbo, Manual power limits), fan curves, CPU/GPU temperature, package power, fan speeds, battery charge
// limit, keyboard light, GPU MUX state. Layout follows macos/RogControl (docs/rog-control-design.md).
//
// Unlike RogControl, this app never re-applies a mode, fan curve, power limit or keyboard light on its own: no such writes
// on launch, refresh, wake, login, power-source changes or exit. Every request comes from a button press. The one
// exception is opt-in ("Restore charge limit at login", default on): the BIOS resets the charge limit on every boot, so
// the last limit the driver accepted is requested once, when the app first finds a driver with no charge request yet. Profile, charge, keyboard and limit highlights show the last
// request the driver accepted, not a firmware readback (this BIOS has none for them).
//
// Talks to kexts/TufWmi through its user client (wire format: kexts/TufWmi/tuf_policy.h, ABI 1 and 2).
// Command line (same binary):
//   TufControl --status       prints the driver state as JSON and exits
//   TufControl --background   starts in the menu bar without opening the window
// Build: scripts/build-tuf-control.sh
import AppKit
import SwiftUI
import Charts
import IOKit
import IOKit.ps

// ---- driver access (tuf_policy.h) --------------------------------------------------------------------------------------------
enum Dev {
    static let profile: UInt64 = 0x00120075, charge: UInt64 = 0x00120057
    static let pl1: UInt64 = 0x001200A3, pl2: UInt64 = 0x001200A0
    static let kbd: UInt64 = 0x00050021, rgb: UInt64 = 0x00100056
}
enum Cap {
    static let profile: UInt32 = 1 << 0, charge: UInt32 = 1 << 1, cpuFan: UInt32 = 1 << 2, gpuFan: UInt32 = 1 << 3
    static let cpuTemp: UInt32 = 1 << 4, gpuTemp: UInt32 = 1 << 5, rapl: UInt32 = 1 << 6, curves: UInt32 = 1 << 7
    static let pl: UInt32 = 1 << 8, kbd: UInt32 = 1 << 9, rgb: UInt32 = 1 << 10, mux: UInt32 = 1 << 11
}
let plRange = (pl1: 15...65, pl2: 25...95)    // TUF_PL1_MIN/MAX, TUF_PL2_MIN/MAX
let unknown = UInt32.max

struct TufState {
    var abi: UInt32 = 0
    var connected = false
    var caps: UInt32 = 0
    var words: [UInt32] = Array(repeating: .max, count: 24)
    func has(_ cap: UInt32) -> Bool { caps & cap != 0 }
    func w(_ i: Int) -> UInt32 { i < words.count ? words[i] : .max }
    var writes: Bool { w(8) == 1 }
    var ac: Bool? { w(9) == unknown ? nil : w(9) == 1 }
    func rawHex(_ i: Int) -> String { String(format: "0x%08X", w(i)) }
    // Raw DSTS fan words 4 (CPU) and 5 (GPU): units of 100 RPM in the low 16 bits.
    private func fanRpm(word: Int, cap: UInt32) -> Int? {
        guard has(cap) else { return nil }
        let v = Int(w(word) & 0xffff); return v <= 150 ? v * 100 : nil
    }
    var cpuFan: Int? { fanRpm(word: 4, cap: Cap.cpuFan) }
    var gpuFan: Int? { fanRpm(word: 5, cap: Cap.gpuFan) }
    var requestedProfile: Int? { w(6) <= 2 ? Int(w(6)) : nil }
    var requestedCharge: Int? { [60, 80, 100].contains(w(7)) ? Int(w(7)) : nil }
    var cpuTemp: Int? { has(Cap.cpuTemp) && w(10) != unknown ? Int(w(10)) : nil }
    var gpuTemp: Int? { has(Cap.gpuTemp) && w(11) != unknown ? Int(w(11)) : nil }
    var energy: (raw: UInt32, joulesPerCount: Double)? {
        guard has(Cap.rapl), w(12) != unknown, w(13) != unknown else { return nil }
        return (w(12), 1.0 / Double(1 << ((w(13) >> 8) & 0x1f)))
    }
    var requestedPL: (Int?, Int?) { (w(14) == unknown ? nil : Int(w(14)), w(15) == unknown ? nil : Int(w(15))) }
    var fanCustom: Bool { w(16) == 1 }
    var requestedKbd: Int? { w(18) <= 3 ? Int(w(18)) : nil }
    var requestedRgb: (value: UInt32, speed: Int)? { w(20) == unknown ? nil : (w(20), Int(w(21))) }
    var mux: UInt32? { has(Cap.mux) ? w(22) : nil }
}

enum Bridge {
    static func call(_ selector: UInt32, inputs: [UInt64] = [], structIn: [UInt8] = [], bytes: Int = 0) -> (kern_return_t, [UInt8]) {
        let service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("TufWmi"))
        guard service != 0 else { return (kIOReturnNotFound, []) }
        defer { IOObjectRelease(service) }
        var connection: io_connect_t = 0
        let opened = IOServiceOpen(service, mach_task_self_, 0, &connection)
        guard opened == KERN_SUCCESS else { return (opened, []) }
        defer { IOServiceClose(connection) }
        var output = [UInt8](repeating: 0, count: bytes)
        var size = bytes
        let result = inputs.withUnsafeBufferPointer { input in
            structIn.withUnsafeBytes { sin in
                output.withUnsafeMutableBytes { out in
                    IOConnectCallMethod(connection, selector, input.baseAddress, UInt32(input.count),
                                        structIn.isEmpty ? nil : sin.baseAddress, structIn.count,
                                        nil, nil, bytes > 0 ? out.baseAddress : nil, &size)
                }
            }
        }
        return (result, Array(output.prefix(size)))
    }
    static func state() -> (kern_return_t, TufState) {
        var state = TufState()
        let (result, bytes) = call(0, bytes: 24 * 4)       // an ABI 1 driver returns its 10 words
        guard result == KERN_SUCCESS, bytes.count >= 40, bytes.count % 4 == 0 else { return (result, state) }
        // Byte-wise decode avoids alignment assumptions about Swift Array<UInt8> storage.
        let words = (0..<bytes.count / 4).map { n in (0..<4).reduce(UInt32(0)) { $0 | UInt32(bytes[n * 4 + $1]) << ($1 * 8) } }
        guard words[0] == 1 || words[0] == 2 else { return (kIOReturnUnsupported, state) }
        state.abi = words[0]; state.connected = true
        state.caps = words[0] == 1 ? words[1] & 0xf : words[1]
        for (i, v) in words.enumerated() where i < state.words.count { state.words[i] = v }
        return (KERN_SUCCESS, state)
    }
    static func set(_ device: UInt64, _ value: Int, _ value2: Int? = nil) -> kern_return_t {
        guard value >= 0, (value2 ?? 0) >= 0 else { return kIOReturnBadArgument }
        return call(1, inputs: [device, UInt64(value)] + (value2.map { [UInt64($0)] } ?? [])).0
    }
    static func curve(fan: Int, index: Int) -> [UInt8]? {
        let (r, b) = call(2, inputs: [UInt64(fan), UInt64(index)], bytes: 16)
        return r == KERN_SUCCESS && b.count == 16 ? b : nil
    }
    static func writeCurve(fan: Int, _ c: [UInt8]) -> kern_return_t { call(3, inputs: [UInt64(fan)], structIn: c).0 }
    static func fanReset() -> kern_return_t { call(4).0 }
}

// macOS power sources and the battery's registry entry (no driver needed)
struct BatteryInfo {
    var percent: Int? = nil, charging = false, onAC: Bool? = nil
    var cycles: Int? = nil, fullMah: Int? = nil, designMah: Int? = nil
    static func read() -> BatteryInfo {
        var b = BatteryInfo()
        if let snapshot = IOPSCopyPowerSourcesInfo()?.takeRetainedValue() {
            if let source = IOPSGetProvidingPowerSourceType(snapshot)?.takeUnretainedValue() { b.onAC = (source as String) == kIOPSACPowerValue }
            if let list = IOPSCopyPowerSourcesList(snapshot)?.takeRetainedValue() as? [CFTypeRef] {
                for source in list {
                    guard let d = IOPSGetPowerSourceDescription(snapshot, source)?.takeUnretainedValue() as? [String: Any],
                          let now = d[kIOPSCurrentCapacityKey] as? Int, let max = d[kIOPSMaxCapacityKey] as? Int, max > 0 else { continue }
                    b.percent = min(100, Swift.max(0, Int(Double(now) * 100 / Double(max))))
                    b.charging = d[kIOPSIsChargingKey] as? Bool ?? false
                    break
                }
            }
        }
        let service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSmartBattery"))
        if service != 0 {
            defer { IOObjectRelease(service) }
            func int(_ key: String) -> Int? { IORegistryEntryCreateCFProperty(service, key as CFString, kCFAllocatorDefault, 0)?.takeRetainedValue() as? Int }
            b.cycles = int("CycleCount"); b.designMah = int("DesignCapacity")
            b.fullMah = int("AppleRawMaxCapacity") ?? int("MaxCapacity").flatMap { $0 > 100 ? $0 : nil }
        }
        return b
    }
}

// ---- modes and saved manual preferences -------------------------------------------------------------------------------------
enum Mode: String, CaseIterable, Identifiable {
    case silent, performance, turbo, manual
    var id: String { rawValue }
    var title: String { ["silent": "Silent", "performance": "Performance", "turbo": "Turbo", "manual": "Manual"][rawValue]! }
    var symbol: String {
        ["silent": "leaf", "performance": "gauge.with.dots.needle.50percent", "turbo": "flame", "manual": "slider.horizontal.3"][rawValue]!
    }
    var firmwareProfile: Int { self == .silent ? 2 : self == .turbo ? 1 : 0 }   // asus-wmi: 0 balanced, 1 overboost, 2 silent
    var curveIndex: Int { self == .silent ? 1 : self == .turbo ? 2 : 0 }        // DSTS curve order: CFCV, CSFV, CTFV
    static func from(profile: Int?) -> Mode? { profile == 2 ? .silent : profile == 1 ? .turbo : profile == 0 ? .performance : nil }
}

// Preferences only: nothing here is applied until the user presses Apply.
struct ManualSettings: Equatable {
    var pl1: Int, pl2: Int, fanBase: Mode
    static func load() -> ManualSettings {
        let d = UserDefaults.standard
        func i(_ k: String, _ def: Int) -> Int { d.object(forKey: k) == nil ? def : d.integer(forKey: k) }
        return ManualSettings(pl1: min(max(i("manual.pl1", 45), plRange.pl1.lowerBound), plRange.pl1.upperBound),
                              pl2: min(max(i("manual.pl2", 65), plRange.pl2.lowerBound), plRange.pl2.upperBound),
                              fanBase: Mode(rawValue: d.string(forKey: "manual.fanBase") ?? "") ?? .performance)
    }
    func save() {
        let d = UserDefaults.standard
        d.set(pl1, forKey: "manual.pl1"); d.set(pl2, forKey: "manual.pl2"); d.set(fanBase.rawValue, forKey: "manual.fanBase")
    }
}

struct RGB: Equatable {
    static let modes = ["Static", "Breathing", "Colour cycle"]
    static let speeds = ["Slow", "Medium", "Fast"]
    var red = 255, green = 80, blue = 0, mode = 0, speed = 1
    var packed: Int { red | green << 8 | blue << 16 | mode << 24 }
    init(red: Int = 255, green: Int = 80, blue: Int = 0, mode: Int = 0, speed: Int = 1) {
        self.red = red; self.green = green; self.blue = blue; self.mode = mode; self.speed = speed
    }
    init(packed v: UInt32, speed s: Int) {
        red = Int(v & 0xff); green = Int((v >> 8) & 0xff); blue = Int((v >> 16) & 0xff); mode = Int((v >> 24) & 0xff); speed = min(2, max(0, s))
    }
    var color: Color { Color(red: Double(red) / 255, green: Double(green) / 255, blue: Double(blue) / 255) }
}

// ---- login item (a user LaunchAgent; off unless the user turns it on; starts in the menu bar, never writes firmware) -------
enum LoginItem {
    static let label = "local.tuf.fx517zm.control"
    static var plist: URL { FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/LaunchAgents/\(label).plist") }
    static var enabled: Bool { FileManager.default.fileExists(atPath: plist.path) }
    static func set(_ on: Bool) {
        if !on { try? FileManager.default.removeItem(at: plist); return }
        let exe = Bundle.main.executablePath ?? CommandLine.arguments[0]
        let d: [String: Any] = ["Label": label, "ProgramArguments": [exe, "--background"], "RunAtLoad": true, "ProcessType": "Interactive",
                                "LimitLoadToSessionType": "Aqua"]
        try? FileManager.default.createDirectory(at: plist.deletingLastPathComponent(), withIntermediateDirectories: true)
        (d as NSDictionary).write(to: plist, atomically: true)
    }
}

// ---- model -------------------------------------------------------------------------------------------------------------------
@MainActor
final class TufModel: ObservableObject {
    @Published var st = TufState()
    @Published var result: kern_return_t? = nil
    @Published var battery = BatteryInfo.read()
    @Published var watts: Double? = nil
    /// A firmware request is in flight; only this disables the controls.
    @Published var busy = false
    @Published var message = ""
    @Published var profileError = ""
    @Published var chargeError = ""
    @Published var lightingError = ""
    @Published var manual = ManualSettings.load()
    @Published var showManual = false
    @Published var curve: [UInt8]? = nil
    @Published var gpuCurve: [UInt8]? = nil
    @Published var curveMode: Mode = .performance
    @Published var rgb = RGB()
    @Published var loginEnabled = LoginItem.enabled
    @Published var restoreCharge = UserDefaults.standard.object(forKey: "charge.restore") as? Bool ?? true
    private var chargeRestoreDone = false
    var onChange: (() -> Void)?
    private var refreshing = false
    private var lastEnergy: (raw: UInt32, at: Date)?
    private var timer: Timer?
    private let worker = DispatchQueue(label: "local.tuf.control.firmware", qos: .utility)
    var visible = false { didSet { if visible != oldValue { schedule() } } }

    var activeMode: Mode? { st.connected ? (st.fanCustom ? .manual : Mode.from(profile: st.requestedProfile)) : nil }
    var shownMode: Mode? { showManual ? .manual : activeMode }
    var ac: Bool? { st.ac ?? battery.onAC }
    var canWrite: Bool { st.connected && st.writes && !busy }
    var modeLabel: String {
        guard st.connected else { return "Unavailable" }
        return activeMode.map { $0 == .manual ? "Manual applied" : "\($0.title) requested" } ?? "No mode request yet"
    }
    var manualDirty: Bool {
        let (p1, p2) = st.requestedPL
        return !st.fanCustom || p1 != manual.pl1 || p2 != max(manual.pl2, manual.pl1) || manual != ManualSettings.load()
    }

    var driverLine: String {
        if st.connected {
            let v = st.abi == 1 ? " (driver 0.1.x: temperatures, curves and Manual need 0.2.0)" : ""
            return (st.writes ? "TufWmi connected · requests enabled" : "TufWmi connected read-only · boot with tufwmi=2 to enable requests") + v
        }
        switch result {
        case kIOReturnNotFound?: return "TufWmi is not loaded · boot through the TUF test USB (F8 → UEFI Kingston)"
        case kIOReturnNotPrivileged?: return "TufWmi refused access · driver 0.1.1 or newer allows admin users"
        case let r?: return "TufWmi error " + String(format: "0x%08x", r)
        case nil: return "Checking TufWmi…"
        }
    }
    var batteryLine: String {
        guard let p = battery.percent else { return "Battery information unavailable" }
        var s = "Battery \(p)%" + (battery.charging ? " · charging" : (ac == true ? " · on AC" : " · discharging"))
        if let f = battery.fullMah, let d = battery.designMah, d > 0 { s += String(format: " · health %.0f%% (%d / %d mAh)", 100.0 * Double(f) / Double(d), f, d) }
        if let c = battery.cycles { s += " · \(c) cycles" }
        return s
    }
    var gpuLine: String {
        guard let m = st.mux else { return "GPU: MUX state not reported" }
        return "GPU: " + (m & 1 == 1 ? "Hybrid (Optimus)" : "dGPU only (MUX)") + " · NVIDIA RTX 3060 Laptop"
    }

    init() {
        if let v = UserDefaults.standard.object(forKey: "rgb") as? Int { rgb = RGB(packed: UInt32(truncatingIfNeeded: v), speed: UserDefaults.standard.integer(forKey: "rgb.speed")) }
        // Restoring the charge limit after a reboot needs the app running at login: enable that once, never again after.
        if restoreCharge && !UserDefaults.standard.bool(forKey: "login.autoEnabled") {
            UserDefaults.standard.set(true, forKey: "login.autoEnabled")
            if !LoginItem.enabled { LoginItem.set(true); loginEnabled = LoginItem.enabled }
        }
        refresh()
        // Reading only: a power-source change refreshes the display and never re-applies a setting.
        let ctx = Unmanaged.passUnretained(self).toOpaque()
        if let src = IOPSNotificationCreateRunLoopSource({ ctx in
            guard let ctx else { return }
            let m = Unmanaged<TufModel>.fromOpaque(ctx).takeUnretainedValue()
            MainActor.assumeIsolated { m.refresh() }
        }, ctx)?.takeRetainedValue() {
            CFRunLoopAddSource(CFRunLoopGetMain(), src, .defaultMode)
        }
        schedule()
    }

    func schedule() {
        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: visible ? 2 : 30, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.refresh() }
        }
    }

    func refresh() {
        battery = BatteryInfo.read()
        guard !busy, !refreshing else { return }
        refreshing = true
        worker.async {
            let (result, snapshot) = Bridge.state()
            DispatchQueue.main.async { self.apply(snapshot, result) }
        }
    }
    private func apply(_ snapshot: TufState, _ result: kern_return_t) {
        let previous = activeMode
        st = snapshot; self.result = result; refreshing = false
        // Package power from the RAPL energy counter (32-bit, wraps): joules since the last sample over elapsed time.
        if let e = snapshot.energy {
            let now = Date()
            if let last = lastEnergy, now.timeIntervalSince(last.at) > 0.5 {
                let delta = Double(e.raw &- last.raw) * e.joulesPerCount
                watts = delta / now.timeIntervalSince(last.at)
            }
            lastEnergy = (e.raw, now)
        } else { watts = nil; lastEnergy = nil }
        if previous != activeMode || (curve == nil && st.has(Cap.curves)) { loadCurve() }
        if let c = st.requestedCharge { UserDefaults.standard.set(c, forKey: "charge.saved") }
        restoreChargeIfNeeded()
        onChange?()
    }

    // After a reboot the driver has no charge request and the BIOS is back at 100 %: request the saved limit once.
    private func restoreChargeIfNeeded() {
        guard !chargeRestoreDone, restoreCharge, st.connected, st.writes, st.has(Cap.charge), !busy else { return }
        chargeRestoreDone = true
        let saved = UserDefaults.standard.integer(forKey: "charge.saved")
        guard st.requestedCharge == nil, [60, 80].contains(saved) else { return }   // 100 % is the BIOS default anyway
        perform("\(saved)% charge limit (restored after restart)", error: { self.chargeError = $0 }) { Bridge.set(Dev.charge, saved) }
    }
    func setRestoreCharge(_ on: Bool) {
        restoreCharge = on; UserDefaults.standard.set(on, forKey: "charge.restore")
        if on && !loginEnabled { setLogin(true) }   // restoring after a reboot needs the app to start at login
    }

    func choose(_ mode: Mode) {
        profileError = ""
        if mode == .manual { showManual = true; return }
        showManual = false
        if mode == .turbo && ac != true { profileError = "Turbo needs the AC adapter."; return }
        // Requesting a profile also ends a custom fan curve and Manual limits (the firmware reloads its own).
        perform("\(mode.title) mode", error: { self.profileError = $0 }) { Bridge.set(Dev.profile, mode.firmwareProfile) }
    }
    func setCharge(_ v: Int) { perform("\(v)% charge limit", error: { self.chargeError = $0 }) { Bridge.set(Dev.charge, v) } }
    func setKbd(_ v: Int) { perform("keyboard light", error: { self.lightingError = $0 }) { Bridge.set(Dev.kbd, v) } }
    func setRgb(_ c: RGB) {
        rgb = c
        UserDefaults.standard.set(c.packed, forKey: "rgb"); UserDefaults.standard.set(c.speed, forKey: "rgb.speed")
        perform("keyboard colour", error: { self.lightingError = $0 }) { Bridge.set(Dev.rgb, c.packed, c.speed) }
    }

    // Manual, as RogControl's Apply.mode: profile first (fresh firmware state), then the chosen firmware fan curves as custom
    // curves (the EC only honours PL1/PL2 in manual-fan mode), then the limits with PL1 <= PL2. Any failure hands the fans
    // back to the firmware by requesting the profile again.
    func applyManual() {
        var s = manual
        if s.fanBase == .turbo && ac != true { s.fanBase = .performance }
        s.pl2 = max(s.pl2, s.pl1)
        manual = s; s.save()
        profileError = ""
        perform("Manual (PL1 \(s.pl1) W, PL2 \(s.pl2) W, \(s.fanBase.title) fans)", error: { self.profileError = $0 }) {
            var r = Bridge.set(Dev.profile, Mode.performance.firmwareProfile)
            guard r == KERN_SUCCESS else { return r }
            for fan in 0..<2 {
                guard let c = Bridge.curve(fan: fan, index: s.fanBase.curveIndex) else { _ = Bridge.fanReset(); return kIOReturnIOError }
                r = Bridge.writeCurve(fan: fan, c)
                guard r == KERN_SUCCESS else { _ = Bridge.fanReset(); return r }
            }
            for (dev, v) in [(Dev.pl1, s.pl1), (Dev.pl2, s.pl2)] {
                r = Bridge.set(dev, v)
                guard r == KERN_SUCCESS else { _ = Bridge.fanReset(); return r }
            }
            return KERN_SUCCESS
        }
        showManual = false
    }
    func resetFans() { perform("automatic fans", error: { self.profileError = $0 }) { Bridge.fanReset() } }

    private func perform(_ label: String, error: @escaping (String) -> Void, _ body: @escaping () -> kern_return_t) {
        guard !busy else { return }
        busy = true; error(""); message = "Requesting \(label)…"
        // Serial worker queue: this runs after any in-flight refresh finishes.
        worker.async {
            let result = body()
            let (stateResult, snapshot) = Bridge.state()
            DispatchQueue.main.async {
                self.busy = false
                self.apply(snapshot, stateResult)
                switch result {
                case KERN_SUCCESS: self.message = "\(label) requested. Firmware returned success."
                case kIOReturnNotPermitted: error("Requests are off in the driver. Boot with tufwmi=2."); self.message = ""
                case kIOReturnUnsupported: error("This BIOS does not report support for \(label)."); self.message = ""
                case kIOReturnBadArgument: error("The driver refused \(label) as out of range."); self.message = ""
                default: error("Firmware did not apply \(label) (" + String(format: "0x%08x", result) + ")."); self.message = ""
                }
                self.loadCurve()
            }
        }
    }

    func loadCurve() {
        // Manual shows the selected firmware reference, not an editable or measured RPM curve.
        var m = showManual || activeMode == .manual ? manual.fanBase : (activeMode ?? .performance)
        if m == .turbo && ac != true { m = .performance }
        curveMode = m
        guard st.has(Cap.curves) else { curve = nil; gpuCurve = nil; return }
        let index = m.curveIndex
        worker.async {
            let c = Bridge.curve(fan: 0, index: index), g = Bridge.curve(fan: 1, index: index)
            DispatchQueue.main.async { self.curve = c; self.gpuCurve = g }
        }
    }
    func setLogin(_ on: Bool) { LoginItem.set(on); loginEnabled = LoginItem.enabled }
}

// ---- views (RogControl design) -----------------------------------------------------------------------------------------------
struct Stat: View {
    let symbol: String, value: String, label: String
    var body: some View {
        VStack(spacing: 2) {
            Image(systemName: symbol).font(.system(size: 13)).foregroundStyle(.secondary)
            Text(value).font(.system(size: 24, weight: .semibold)).monospacedDigit()
            Text(label).font(.system(size: 10)).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
    }
}

// ABI: eight temperatures followed by eight fan duties. Never extrapolate or invent replacements for invalid points.
struct FanCurvePoint: Identifiable { let id: Int, temperature: Int, duty: Int, segment: Int }
struct FanCurveData {
    let points: [FanCurvePoint]
    let omitted: Int
    init(_ bytes: [UInt8]) {
        guard bytes.count == 16 else { points = []; omitted = bytes.isEmpty ? 0 : 8; return }
        var valid: [FanCurvePoint] = []
        var skipped = 0, segment = 0
        var previous: Int?
        for i in 0..<8 {
            let temperature = Int(bytes[i]), duty = Int(bytes[8 + i])
            guard (20...105).contains(temperature), (0...100).contains(duty), previous == nil || temperature >= previous! else {
                skipped += 1; segment += 1; continue
            }
            valid.append(FanCurvePoint(id: i, temperature: temperature, duty: duty, segment: segment))
            previous = temperature
        }
        points = valid; omitted = skipped
    }
}

struct CurveChart: View {
    let c: [UInt8]
    let compact: Bool
    @State private var hovered: Int?
    private var data: FanCurveData { FanCurveData(c) }
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Fan duty (%)").font(.caption).foregroundStyle(.secondary)
                Spacer()
                if let point = data.points.first(where: { $0.id == hovered }) {
                    Text("\(point.temperature)°C · \(point.duty)%").font(.caption).monospacedDigit()
                }
            }.frame(height: 16)
            if data.points.isEmpty {
                Text("No valid fan curve data.").font(.callout).foregroundStyle(.secondary).frame(height: 100)
            } else {
                Chart(data.points) { point in
                    LineMark(x: .value("Temperature", point.temperature), y: .value("Fan duty", point.duty),
                             series: .value("Segment", point.segment))
                        .interpolationMethod(.linear).lineStyle(StrokeStyle(lineWidth: 2))
                        .foregroundStyle(Color.accentColor)
                    PointMark(x: .value("Temperature", point.temperature), y: .value("Fan duty", point.duty))
                        .symbolSize(point.id == hovered ? 70 : 24).foregroundStyle(Color.accentColor)
                }
                .chartXScale(domain: 20...105).chartYScale(domain: 0...100)
                .chartXAxis {
                    AxisMarks(values: [20, 40, 60, 80, 100]) { value in
                        AxisGridLine(); AxisTick()
                        AxisValueLabel { if let n = value.as(Int.self) { Text("\(n)°") } }
                    }
                }
                .chartYAxis {
                    AxisMarks(position: .leading, values: [0, 25, 50, 75, 100]) { value in
                        AxisGridLine(); AxisTick()
                        AxisValueLabel { if let n = value.as(Int.self) { Text("\(n)%") } }
                    }
                }
                .chartLegend(.hidden)
                .chartOverlay { proxy in
                    GeometryReader { geometry in
                        Rectangle().fill(.clear).contentShape(Rectangle())
                            .onContinuousHover { phase in
                                switch phase {
                                case .active(let location):
                                    let origin = geometry[proxy.plotAreaFrame].origin
                                    let local = CGPoint(x: location.x - origin.x, y: location.y - origin.y)
                                    hovered = data.points.min { a, b in distance(a, to: local, proxy: proxy) < distance(b, to: local, proxy: proxy) }?.id
                                case .ended: hovered = nil
                                }
                            }
                    }
                }
                .frame(height: compact ? 130 : 220)
                Text("Temperature (°C)").font(.caption).foregroundStyle(.secondary).frame(maxWidth: .infinity)
            }
            if data.omitted > 0 {
                Text("\(data.omitted) firmware point(s) outside the valid range or order are not plotted.")
                    .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            }
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel("Fan duty by temperature")
        .accessibilityValue(data.points.map { "\($0.temperature) degrees Celsius, \($0.duty) percent" }.joined(separator: "; "))
    }
    private func distance(_ point: FanCurvePoint, to position: CGPoint, proxy: ChartProxy) -> CGFloat {
        guard let x = proxy.position(forX: point.temperature), let y = proxy.position(forY: point.duty) else { return .infinity }
        return pow(x - position.x, 2) + pow(y - position.y, 2)
    }
}

struct FanCurveView: View {
    @ObservedObject var m: TufModel
    var compact = false
    @State private var fan = 0
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("\(m.curveMode.title) firmware curve").font(.system(size: 12, weight: .medium))
                Spacer()
                Picker("Fan", selection: $fan) { Text("CPU").tag(0); Text("GPU").tag(1) }
                    .pickerStyle(.segmented).frame(width: 105).controlSize(.small)
            }
            if let bytes = fan == 0 ? m.curve : m.gpuCurve { CurveChart(c: bytes, compact: compact) }
            else { Text(m.st.has(Cap.curves) ? "Reading fan curve…" : "Fan curves need TufWmi 0.2.0.").font(.caption).foregroundStyle(.secondary) }
            if !compact {
                Text("Firmware reference, not live RPM. Hover a point to read its temperature and duty.").font(.caption).foregroundStyle(.secondary)
            }
        }
    }
}

struct LimitSlider: View {
    let name: String, help: String
    @Binding var value: Int
    let range: ClosedRange<Int>
    var body: some View {
        HStack {
            Text(name).font(.system(size: 12)).frame(width: 44, alignment: .leading).help(help)
            Slider(value: Binding(get: { Double(value) }, set: { value = Int($0.rounded()) }),
                   in: Double(range.lowerBound)...Double(range.upperBound)).controlSize(.small)
            Text("\(value) W").font(.system(size: 12)).monospacedDigit().frame(width: 40, alignment: .trailing)
        }
    }
}

enum TufPage: String, CaseIterable, Identifiable {
    case overview = "Overview", performance = "Performance", lighting = "Lighting", battery = "Battery & Graphics", settings = "Settings"
    var id: String { rawValue }
    var symbol: String {
        switch self {
        case .overview: return "square.grid.2x2"
        case .performance: return "gauge.with.dots.needle.50percent"
        case .lighting: return "keyboard"
        case .battery: return "battery.75"
        case .settings: return "gearshape"
        }
    }
}

struct TufSection<Content: View>: View {
    let title: String
    @ViewBuilder var content: Content
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(title).font(.system(size: 15, weight: .semibold))
            content
        }
        .padding(20).frame(maxWidth: .infinity, alignment: .leading)
        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 12))
        .overlay(RoundedRectangle(cornerRadius: 12).strokeBorder(Color.primary.opacity(0.08)))
    }
}

// Mode tiles take plain values so their identity follows the highlighted mode (see RogControl's ModeRow).
struct ModeRow: View {
    let active: Mode?
    let ac: Bool?
    let compact: Bool
    let manualAvailable: Bool
    let choose: (Mode) -> Void
    var body: some View {
        HStack(spacing: compact ? 4 : 8) {
            ForEach(Mode.allCases) { mode in
                let selected = active == mode
                let unavailable = (mode == .turbo && ac != true) || (mode == .manual && !manualAvailable)
                Button { choose(mode) } label: {
                    VStack(spacing: compact ? 5 : 8) {
                        Image(systemName: mode.symbol).font(.system(size: compact ? 16 : 20))
                        Text(mode.title).font(.system(size: compact ? 11 : 12, weight: .medium))
                    }
                    .frame(maxWidth: .infinity).padding(.vertical, compact ? 6 : 16)
                    .foregroundStyle(selected ? Color.accentColor : Color.primary)
                    .background(selected ? Color.accentColor.opacity(0.12) : Color.primary.opacity(0.04), in: RoundedRectangle(cornerRadius: 8))
                    .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(selected ? Color.accentColor : Color.clear))
                }
                .buttonStyle(.plain).disabled(unavailable).opacity(unavailable ? 0.4 : 1)
                .accessibilityLabel(mode.title + (selected ? ", selected" : ""))
                .help(mode == .turbo && ac != true ? "Turbo needs the AC adapter" : unavailable ? "Needs TufWmi 0.2.0 with fan curve and power limit support" : mode.title)
            }
        }
        .id("mode-\(active?.rawValue ?? "none")-\(String(describing: ac))-\(manualAvailable)")
    }
}

struct ChoiceButton: View {
    let title: String
    let selected: Bool
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(title).font(.system(size: 12, weight: selected ? .semibold : .regular))
                .fixedSize().padding(.horizontal, 8).padding(.vertical, 5)
                .foregroundStyle(selected ? Color.accentColor : Color.primary)
                .background(selected ? Color.accentColor.opacity(0.12) : Color.primary.opacity(0.04), in: RoundedRectangle(cornerRadius: 6))
                .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(selected ? Color.accentColor : Color.clear))
        }.buttonStyle(.plain).accessibilityLabel(title + (selected ? ", last requested" : ""))
    }
}

struct KeyboardRow: View {
    let level: Int?
    let set: (Int) -> Void
    var body: some View {
        HStack(spacing: 6) {
            Text("Keyboard").font(.system(size: 12))
            ForEach(0...3, id: \.self) { v in
                ChoiceButton(title: ["Off", "Low", "Med", "High"][v], selected: level == v) { set(v) }
            }
            Spacer()
        }
        .id("kbd-\(level ?? -1)")
        .help("Keyboard backlight brightness")
    }
}

func inlineError(_ text: String) -> some View {
    Group {
        if !text.isEmpty {
            Label(text, systemImage: "exclamationmark.triangle").font(.caption)
                .foregroundStyle(.red).fixedSize(horizontal: false, vertical: true)
        }
    }
}
func fmt(_ v: Int?, _ unit: String) -> String { v.map { "\($0)\(unit)" } ?? "–" }

struct TufControls: View {
    @ObservedObject var m: TufModel
    let page: TufPage
    var compact = false
    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            if !m.st.connected {
                Label(m.driverLine, systemImage: "exclamationmark.triangle").font(.callout).foregroundStyle(.secondary)
            } else if !m.st.writes {
                Label("Read-only driver mode. Readings work; requests stay off until you boot with tufwmi=2.", systemImage: "lock")
                    .font(.callout).foregroundStyle(.secondary)
            }
            if page == .overview {
                TufSection(title: "Live telemetry") {
                    HStack(spacing: 16) {
                        Stat(symbol: "thermometer.medium", value: fmt(m.st.cpuTemp, "°"), label: "CPU temperature")
                        Stat(symbol: "bolt", value: m.watts.map { String(format: "%.1f W", $0) } ?? "–", label: "Package power")
                        Stat(symbol: "thermometer.medium", value: fmt(m.st.gpuTemp, "°"), label: "GPU temperature")
                        Stat(symbol: "fan", value: fmt(m.st.cpuFan, ""), label: "CPU fan · RPM")
                        Stat(symbol: "fan", value: fmt(m.st.gpuFan, ""), label: "GPU fan · RPM")
                    }
                }
            }
            if page == .overview || page == .performance {
                TufSection(title: "Performance mode") {
                    VStack(alignment: .leading, spacing: 10) {
                        modes
                        Text(profileCaption).font(.callout).foregroundStyle(.secondary)
                        inlineError(m.profileError)
                    }
                }
            }
            if page == .performance {
                TufSection(title: "Power & cooling") {
                    VStack(alignment: .leading, spacing: 12) {
                        manualControls
                        FanCurveView(m: m)
                        if m.shownMode != .manual {
                            Text("Firmware manages power limits in this mode. Choose Manual to set PL1 / PL2.").font(.callout).foregroundStyle(.secondary)
                        }
                        Text("GPU temperature is the EC's GPU-side sensor (the one NVIDIA Dynamic Boost uses), not the GPU die sensor.")
                            .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
            if page == .lighting {
                TufSection(title: "Keyboard lighting") {
                    VStack(alignment: .leading, spacing: 12) {
                        lightingControls
                        inlineError(m.lightingError)
                        if !m.st.has(Cap.kbd) && !m.st.has(Cap.rgb) {
                            Text(m.st.abi >= 2 ? "This BIOS does not report keyboard light control through WMI." : "Keyboard lighting needs TufWmi 0.2.0.")
                                .foregroundStyle(.secondary)
                        }
                    }
                }
            }
            if page == .overview || page == .battery {
                TufSection(title: "Battery care") {
                    VStack(alignment: .leading, spacing: 12) {
                        batteryControls
                        Text("60% for desk use · 80% for everyday use · 100% for travel").font(.callout).foregroundStyle(.secondary)
                        Toggle("Restore charge limit at login", isOn: Binding(get: { m.restoreCharge }, set: { m.setRestoreCharge($0) }))
                            .toggleStyle(.switch).controlSize(.small).font(.callout)
                            .help("The BIOS resets the limit to 100% on every boot; TUF Control requests your last limit once when it starts.")
                        if page == .battery {
                            Text("A lower limit stops further charging; it does not discharge the battery. This BIOS resets the limit to 100% on every boot; with \"Restore charge limit at login\" on, TUF Control requests your last limit again when it starts.")
                                .font(.callout).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                        }
                    }
                }
            }
            if page == .overview || page == .battery {
                TufSection(title: "Graphics") { Text(m.gpuLine).font(.callout).foregroundStyle(.secondary) }
            }
            if page == .settings {
                TufSection(title: "Startup") {
                    Toggle("Open at Login", isOn: Binding(get: { m.loginEnabled }, set: { m.setLogin($0) })).toggleStyle(.switch)
                    Text("Starts quietly in the menu bar at login. It only reads status; it never changes a setting by itself.")
                        .font(.callout).foregroundStyle(.secondary)
                }
                TufSection(title: "Driver details") {
                    VStack(alignment: .leading, spacing: 6) {
                        Text(m.driverLine)
                        if m.st.connected {
                            Text("ABI \(m.st.abi) · capabilities 0x\(String(m.st.caps, radix: 16))")
                            Text("Profile \(m.st.rawHex(2)) · charge \(m.st.rawHex(3)) (raw; not a verified readback)")
                            Text("CPU fan \(m.st.rawHex(4)) · GPU fan \(m.st.rawHex(5))")
                            if m.st.abi >= 2 {
                                Text("Keyboard \(m.st.rawHex(17)) · RGB \(m.st.rawHex(19)) · MUX \(m.st.rawHex(22)) · dGPU \(m.st.rawHex(23))")
                                Text("Custom fans \(m.st.fanCustom ? "on" : "off") · RAPL \(m.st.has(Cap.rapl) ? "on" : "off")")
                            }
                        }
                    }.font(.system(size: 12).monospaced()).foregroundStyle(.secondary).textSelection(.enabled)
                }
                TufSection(title: "About TUF Control") {
                    Text("ASUS TUF Dash F15 · FX517ZM").font(.headline)
                    Text("Changes happen only when you press a button. Highlighted choices are the last requests the driver accepted, not firmware readbacks.")
                        .foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                    Link("Design and WMNB transport by thanhnndev · github.com/thanhnndev", destination: URL(string: "https://github.com/thanhnndev")!)
                    Link("Inspired by asusctl and the asus-linux community", destination: URL(string: "https://asus-linux.org")!)
                }
            }
            if !m.message.isEmpty {
                Label(m.message, systemImage: "info.circle").font(.callout).foregroundStyle(.secondary).textSelection(.enabled)
            }
        }
    }
    var modes: some View {
        ModeRow(active: m.shownMode, ac: m.ac, compact: compact, manualAvailable: m.st.has(Cap.curves) && m.st.has(Cap.pl)) { m.choose($0) }
            .disabled(!m.canWrite || !m.st.has(Cap.profile))
    }
    private var profileCaption: String {
        if m.activeMode == .manual, case let (p1?, p2?) = m.st.requestedPL { return "Manual applied: PL1 \(p1) W · PL2 \(p2) W · custom fans" }
        if let mode = m.activeMode { return "Last request: \(mode.title) · not a firmware readback" }
        return m.st.connected ? "No mode request since the driver started." : "Connect the driver to change the mode."
    }
    var manualControls: some View {
        VStack(alignment: .leading, spacing: 8) {
            if m.shownMode == .manual {
                VStack(alignment: .leading, spacing: 4) {
                    LimitSlider(name: "PL1", help: "Sustained CPU power", value: $m.manual.pl1, range: plRange.pl1)
                    LimitSlider(name: "PL2", help: "Short boost CPU power (kept at or above PL1)", value: $m.manual.pl2, range: plRange.pl2)
                    HStack {
                        Text("Fans").font(.system(size: 12))
                        Picker("", selection: $m.manual.fanBase) {
                            ForEach([Mode.silent, .performance, .turbo]) { Text($0.title + " curve").tag($0) }
                        }
                        .labelsHidden().controlSize(.small).frame(width: 150)
                        .onChange(of: m.manual.fanBase) { _ in m.loadCurve() }
                        Spacer()
                        if m.st.fanCustom { Button("Automatic fans") { m.resetFans() }.controlSize(.small) }
                        Button("Apply") { m.applyManual() }.controlSize(.small).disabled(!m.canWrite)
                    }
                    Text(m.manualDirty ? "Changes not applied" : "Applied").font(.caption).foregroundStyle(.secondary)
                }
            }
        }
    }
    var lightingControls: some View {
        VStack(alignment: .leading, spacing: 10) {
            if m.st.has(Cap.kbd) { KeyboardRow(level: m.st.requestedKbd) { m.setKbd($0) }.disabled(!m.canWrite) }
            if m.st.has(Cap.rgb) {
                HStack(spacing: 8) {
                    Text("Colour").font(.system(size: 12))
                    ColorPicker("", selection: Binding(get: { m.rgb.color }, set: { color in
                        guard let c = NSColor(color).usingColorSpace(.sRGB) else { return }
                        var v = m.rgb; v.red = Int(c.redComponent * 255); v.green = Int(c.greenComponent * 255); v.blue = Int(c.blueComponent * 255)
                        m.rgb = v
                    }), supportsOpacity: false).labelsHidden()
                    Picker("", selection: $m.rgb.mode) { ForEach(0..<RGB.modes.count, id: \.self) { Text(RGB.modes[$0]).tag($0) } }
                        .labelsHidden().controlSize(.small).frame(width: 120)
                    Picker("", selection: $m.rgb.speed) { ForEach(0..<RGB.speeds.count, id: \.self) { Text(RGB.speeds[$0]).tag($0) } }
                        .labelsHidden().controlSize(.small).frame(width: 90).disabled(m.rgb.mode == 0)
                    Button("Apply") { m.setRgb(m.rgb) }.controlSize(.small)
                    Spacer()
                }.disabled(!m.canWrite)
                Text("Colour changes apply until restart; they are not saved to the BIOS.").font(.caption).foregroundStyle(.secondary)
            }
        }
    }
    var batteryControls: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 6) {
                ForEach([60, 80, 100], id: \.self) { v in
                    ChoiceButton(title: "\(v)%", selected: m.st.requestedCharge == v) { m.setCharge(v) }
                }
                Spacer()
                Text(m.st.requestedCharge.map { "requested \($0)%" } ?? "no request yet").font(.system(size: 11)).foregroundStyle(.secondary)
            }.disabled(!m.canWrite || !m.st.has(Cap.charge))
            inlineError(m.chargeError)
            Text(m.batteryLine).font(.system(size: 11)).foregroundStyle(.secondary)
        }
    }
}

// The popover's manual and lighting rows are TufControls sections wrapped in their own View with its own @ObservedObject
// (see RogControl's PopoverPart).
struct PopoverPart: View {
    enum Part { case manual, lighting }
    @ObservedObject var m: TufModel
    let part: Part
    @ViewBuilder var body: some View {
        let c = TufControls(m: m, page: .overview, compact: true)
        switch part {
        case .manual: c.manualControls
        case .lighting: c.lightingControls
        }
    }
}

// Primary surface: stable compact rows, immediate controls, optional details (RogControl's popover layout).
struct ContentView: View {
    @ObservedObject var m: TufModel
    let openWindow: () -> Void
    @State private var showCurves = false
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("TUF Control").font(.system(size: 13, weight: .semibold))
                Spacer()
                Label(m.st.connected || m.ac != nil ? (m.ac == true ? "AC" : "Battery") : "Disconnected", systemImage: m.ac == true ? "powerplug" : "battery.75")
                    .font(.caption).foregroundStyle(.secondary)
            }
            HStack(spacing: 6) {
                quickStat(fmt(m.st.cpuTemp, "°C"), "CPU")
                quickStat(m.watts.map { String(format: "%.1f W", $0) } ?? "–", "Power")
                quickStat(fmt(m.st.gpuTemp, "°C"), "GPU")
                quickStat(fmt(m.st.cpuFan, ""), "CPU RPM")
                quickStat(fmt(m.st.gpuFan, ""), "GPU RPM")
            }
            TufControls(m: m, page: .overview, compact: true).modes
            HStack {
                Text("Active: \(m.modeLabel)").font(.caption).foregroundStyle(.secondary)
                Spacer()
                if m.st.connected && !m.st.writes { Text("Read-only").font(.caption).foregroundStyle(.secondary) }
            }
            inlineError(m.profileError)
            if m.shownMode == .manual { PopoverPart(m: m, part: .manual) }
            Divider()
            HStack(spacing: 8) {
                Text("Charge limit").font(.system(size: 12)).frame(width: 76, alignment: .leading)
                ForEach([60, 80, 100], id: \.self) { value in
                    ChoiceButton(title: "\(value)%", selected: m.st.requestedCharge == value) { m.setCharge(value) }
                }
                Spacer(minLength: 0)
            }.disabled(!m.canWrite || !m.st.has(Cap.charge))
            inlineError(m.chargeError)
            HStack {
                Text(m.battery.percent.map { "Battery \($0)%" } ?? "Battery –")
                Spacer()
                Text(m.battery.charging ? "Charging" : m.ac == true ? "On AC" : "Discharging")
            }.font(.caption).foregroundStyle(.secondary)
            if m.st.has(Cap.kbd) || m.st.has(Cap.rgb) {
                Divider()
                PopoverPart(m: m, part: .lighting)
                inlineError(m.lightingError)
            }
            if m.st.has(Cap.curves) {
                DisclosureGroup("Fan curves", isExpanded: $showCurves) {
                    ScrollView { FanCurveView(m: m, compact: true).padding(.top, 8) }.frame(height: curveDetailHeight)
                }.font(.caption)
            }
            if !m.st.connected { Text(m.driverLine).font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true) }
            if !m.message.isEmpty { Text(m.message).font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true) }
            Divider()
            HStack {
                Menu("Settings") {
                    Toggle("Open at Login", isOn: Binding(get: { m.loginEnabled }, set: { m.setLogin($0) }))
                    Text(m.batteryLine)
                    Text(m.gpuLine)
                    Text(m.driverLine)
                    Button("Created by thanhnndev (GitHub)") { NSWorkspace.shared.open(URL(string: "https://github.com/thanhnndev")!) }
                    Button("Quit TUF Control") { NSApp.terminate(nil) }
                }.menuStyle(.borderlessButton).fixedSize().font(.caption)
                Spacer()
                Button("Open app", action: openWindow).buttonStyle(.borderless).font(.caption)
            }
        }.padding(14).frame(width: 380)
    }
    private var curveDetailHeight: CGFloat {
        let available = (NSScreen.main?.visibleFrame.height ?? 800) - (m.shownMode == .manual ? 590 : 490)
        return min(210, max(95, available))
    }
    private func quickStat(_ value: String, _ label: String) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(value).font(.system(size: 14, weight: .semibold)).monospacedDigit().lineLimit(1).minimumScaleFactor(0.8)
            Text(label).font(.system(size: 10)).foregroundStyle(.secondary)
        }.frame(maxWidth: .infinity, alignment: .leading)
    }
}

struct TufWindowView: View {
    @ObservedObject var m: TufModel
    @State private var page: TufPage = .overview
    var body: some View {
        HStack(spacing: 0) {
            VStack(alignment: .leading, spacing: 20) {
                VStack(alignment: .leading, spacing: 6) {
                    Text("TUF Control").font(.system(size: 21, weight: .semibold))
                    Text("TUF DASH F15").font(.system(size: 10, weight: .medium)).tracking(1.5).foregroundStyle(.secondary)
                }.padding(.horizontal, 12).padding(.top, 20)
                VStack(spacing: 4) {
                    ForEach(TufPage.allCases) { destination in
                        Button { page = destination } label: {
                            Label(destination.rawValue, systemImage: destination.symbol)
                                .font(.system(size: 13, weight: page == destination ? .semibold : .regular))
                                .frame(maxWidth: .infinity, alignment: .leading).padding(12)
                                .foregroundStyle(page == destination ? Color.accentColor : Color.primary)
                                .background(page == destination ? Color.accentColor.opacity(0.12) : Color.clear, in: RoundedRectangle(cornerRadius: 8))
                        }.buttonStyle(.plain)
                    }
                }
                Spacer()
                Label(m.ac == true ? "Connected to AC" : m.ac == false ? "On battery" : "Power source unknown",
                      systemImage: m.ac == true ? "powerplug" : "battery.75")
                    .font(.caption).foregroundStyle(.secondary).padding(12)
            }.padding(12).frame(width: 210).background(Color(nsColor: .windowBackgroundColor))
            Divider()
            ScrollView {
                VStack(alignment: .leading, spacing: 24) {
                    VStack(alignment: .leading, spacing: 8) {
                        Text(page.rawValue).font(.system(size: 24, weight: .semibold))
                        Text("Active mode: \(m.modeLabel)").font(.callout).foregroundStyle(.secondary)
                    }
                    TufControls(m: m, page: page)
                }.padding(32).frame(maxWidth: 900, alignment: .leading).frame(maxWidth: .infinity)
            }.background(Color(nsColor: .underPageBackgroundColor))
        }.frame(minWidth: 800, minHeight: 600)
    }
}

// ---- status item -------------------------------------------------------------------------------------------------------------
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSPopoverDelegate, NSWindowDelegate {
    let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
    let popover = NSPopover()
    var model: TufModel!
    var window: NSWindow?

    func applicationDidFinishLaunching(_ n: Notification) {
        model = TufModel()
        popover.behavior = .transient
        popover.animates = false
        popover.delegate = self
        popover.contentViewController = NSHostingController(rootView: ContentView(m: model, openWindow: { [weak self] in self?.showWindow() }))
        item.button?.target = self
        item.button?.action = #selector(toggle)
        model.onChange = { [weak self] in self?.updateIcon() }
        updateIcon()
        setupMenus()
        if !CommandLine.arguments.contains("--background") { showWindow() }
    }

    @objc func toggle() {
        guard let b = item.button else { return }
        if popover.isShown { popover.performClose(nil); return }
        model.refresh()
        model.visible = true
        popover.show(relativeTo: b.bounds, of: b, preferredEdge: .minY)
        popover.contentViewController?.view.window?.makeKey()
    }
    func popoverDidClose(_ n: Notification) { updateVisibility() }
    func updateVisibility() { model.visible = popover.isShown || window?.isVisible == true }
    func windowWillClose(_ n: Notification) {
        model.visible = popover.isShown
        NSApp.setActivationPolicy(.accessory)
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        showWindow(); return true
    }
    @objc func showWindow() {
        popover.performClose(nil)
        if window == nil {
            let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 980, height: 720),
                             styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
            w.title = "TUF Control"
            w.isReleasedWhenClosed = false
            w.minSize = NSSize(width: 800, height: 640)
            w.contentViewController = NSHostingController(rootView: TufWindowView(m: model))
            w.delegate = self
            w.center()
            w.setFrameAutosaveName("TufControl.MainWindow")
            window = w
        }
        NSApp.setActivationPolicy(.regular)
        window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        model.refresh(); updateVisibility()
    }
    @objc func refreshNow() { model.refresh() }
    @objc func showAbout() {
        let credits = NSMutableAttributedString(string: "FX517ZM adaptation of ROG Control by thanhnndev\ngithub.com/thanhnndev\n\nInspired by asusctl and the asus-linux community.",
                                                attributes: [.font: NSFont.systemFont(ofSize: 11), .foregroundColor: NSColor.labelColor])
        credits.addAttribute(.link, value: URL(string: "https://github.com/thanhnndev")!, range: (credits.string as NSString).range(of: "github.com/thanhnndev"))
        NSApp.orderFrontStandardAboutPanel(options: [.credits: credits])
        NSApp.activate(ignoringOtherApps: true)
    }
    func setupMenus() {
        let menu = NSMenu()
        let appItem = NSMenuItem(); menu.addItem(appItem)
        let appMenu = NSMenu(); appItem.submenu = appMenu
        let about = appMenu.addItem(withTitle: "About TUF Control", action: #selector(showAbout), keyEquivalent: "")
        about.target = self
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Hide TUF Control", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Quit TUF Control", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        let windowItem = NSMenuItem(); menu.addItem(windowItem)
        let windowMenu = NSMenu(title: "Window"); windowItem.submenu = windowMenu
        let open = windowMenu.addItem(withTitle: "Open TUF Control", action: #selector(showWindow), keyEquivalent: "0"); open.target = self
        let refresh = windowMenu.addItem(withTitle: "Refresh", action: #selector(refreshNow), keyEquivalent: "r"); refresh.target = self
        windowMenu.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
        windowMenu.addItem(withTitle: "Close", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        NSApp.mainMenu = menu
        NSApp.windowsMenu = windowMenu
    }

    func updateIcon() {
        guard let b = item.button else { return }
        let symbol = model.activeMode?.symbol ?? "fanblades"
        let img = NSImage(systemSymbolName: symbol, accessibilityDescription: "TUF: \(model.modeLabel)")
        img?.isTemplate = true
        b.image = img
        var tip = "TUF: \(model.modeLabel)"
        if let t = model.st.cpuTemp { tip += " · CPU \(t)°C" }
        if let w = model.watts { tip += String(format: " · %.1f W", w) }
        if let t = model.st.gpuTemp { tip += " · GPU \(t)°C" }
        b.toolTip = tip
    }
}

// ---- command line ------------------------------------------------------------------------------------------------------------
if CommandLine.arguments.contains("--status") {
    let (result, s) = Bridge.state()
    var json: [String: Any] = ["abi": s.abi, "connected": s.connected, "result": String(format: "0x%08x", result),
                               "capabilities": String(format: "0x%x", s.caps), "words": s.words.map { String(format: "0x%08X", $0) },
                               "writesEnabled": s.writes, "acPower": s.ac as Any? ?? NSNull(),
                               "cpuTempC": s.cpuTemp as Any? ?? NSNull(), "gpuTempC": s.gpuTemp as Any? ?? NSNull(),
                               "cpuFanRpm": s.cpuFan as Any? ?? NSNull(), "gpuFanRpm": s.gpuFan as Any? ?? NSNull(),
                               "fanCustom": s.fanCustom, "batteryPercent": BatteryInfo.read().percent as Any? ?? NSNull()]
    if s.has(Cap.curves) {   // read-only DSTS: the firmware's own curves, CPU and GPU, per mode
        for mode in [Mode.silent, .performance, .turbo] {
            json["curve.\(mode.rawValue)"] = (0..<2).map { fan in Bridge.curve(fan: fan, index: mode.curveIndex).map { $0.map(Int.init) } as Any? ?? NSNull() }
        }
    }
    if let e1 = s.energy {   // package power over one second
        Thread.sleep(forTimeInterval: 1)
        if let e2 = Bridge.state().1.energy { json["packageWatts"] = (Double(e2.raw &- e1.raw) * e1.joulesPerCount * 10).rounded() / 10 }
    }
    if let data = try? JSONSerialization.data(withJSONObject: json, options: [.prettyPrinted, .sortedKeys]), let str = String(data: data, encoding: .utf8) { print(str) }
    exit(0)
}

MainActor.assumeIsolated {                     // top-level code runs on the main thread
    let app = NSApplication.shared
    let delegate = AppDelegate()
    app.delegate = delegate
    app.setActivationPolicy(.accessory)
    app.run()
}
