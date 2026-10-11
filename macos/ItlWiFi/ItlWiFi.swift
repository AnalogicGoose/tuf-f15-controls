// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// ItlWiFi: menu bar Wi-Fi control for itlmt7925 (scan list, join with a password, disconnect, signal in the menu bar).
// Talks to the kext through its user client (ABI: kexts/itlmt7925/itl_uc_abi.h) and reads the wifi-* registry properties.
// Passwords are kept in the login keychain (service com.itlmt7925.wifi, account = SSID), never in the kext's Info.plist.
// Build: scripts/build-itlwifi.sh
import AppKit
import IOKit
import Security

// ---- driver access -------------------------------------------------------------------------------------------------------
struct WifiNet { let ssid: String; let channel: Int; let rssi: Int; let security: String
    var joinable: Bool { security == "wpa2" || security == "wpa2/wpa3" }
    var locked: Bool { security != "open" } }

struct WifiStatus {
    var present = false, state = "absent", error = "", ssid = "", channel = 0, rssi = 0, rate = 0, scanGen = 0
    var nets: [WifiNet] = []
}

enum Driver {
    static let abi: UInt32 = 1
    enum Sel: UInt32 { case scan = 0, join = 1, disconnect = 2 }

    static func service() -> io_service_t { IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("itlmt7925")) }

    static func status() -> WifiStatus {
        var s = WifiStatus()
        let svc = service(); guard svc != 0 else { return s }
        defer { IOObjectRelease(svc) }
        func prop(_ k: String) -> Any? { IORegistryEntryCreateCFProperty(svc, k as CFString, kCFAllocatorDefault, 0)?.takeRetainedValue() }
        func num(_ k: String) -> Int { (prop(k) as? NSNumber).map { Int(Int32(truncatingIfNeeded: $0.int64Value)) } ?? 0 }
        s.present = true
        s.state = prop("wifi-state") as? String ?? "off"
        s.error = prop("wifi-error") as? String ?? ""
        s.ssid = prop("wifi-ssid") as? String ?? ""
        s.channel = num("wifi-channel"); s.rssi = num("wifi-rssi"); s.rate = num("wifi-rate"); s.scanGen = num("wifi-scan-gen")
        for d in prop("wifi-networks") as? [[String: Any]] ?? [] {
            guard let ssid = d["ssid"] as? String, !ssid.isEmpty else { continue }
            let rssi = (d["rssi"] as? NSNumber).map { Int(Int32(truncatingIfNeeded: $0.int64Value)) } ?? 0
            s.nets.append(WifiNet(ssid: ssid, channel: (d["channel"] as? NSNumber)?.intValue ?? 0, rssi: rssi,
                                  security: d["security"] as? String ?? "?"))
        }
        // one entry per SSID: the strongest BSS
        var best: [String: WifiNet] = [:]
        for n in s.nets { if let b = best[n.ssid], (b.rssi != 0 && b.rssi >= n.rssi) || n.rssi == 0 { continue }; best[n.ssid] = n }
        s.nets = best.values.sorted { ($0.rssi == 0 ? -200 : $0.rssi) > ($1.rssi == 0 ? -200 : $1.rssi) }
        return s
    }

    @discardableResult
    static func call(_ sel: Sel, input: UnsafeRawBufferPointer? = nil) -> kern_return_t {
        let svc = service(); guard svc != 0 else { return kIOReturnNotFound }
        defer { IOObjectRelease(svc) }
        var conn: io_connect_t = 0
        var kr = IOServiceOpen(svc, mach_task_self_, 0, &conn)
        guard kr == KERN_SUCCESS else { return kr }
        defer { IOServiceClose(conn) }
        kr = IOConnectCallStructMethod(conn, sel.rawValue, input?.baseAddress, input?.count ?? 0, nil, nil)
        return kr
    }

    static func join(ssid: String, psk: String) -> kern_return_t {
        // struct itl_uc_join { uint32_t abi; char ssid[36]; char psk[68]; }
        var buf = [UInt8](repeating: 0, count: 4 + 36 + 68)
        withUnsafeBytes(of: abi.littleEndian) { buf.replaceSubrange(0..<4, with: $0) }
        let s = Array(ssid.utf8.prefix(32)), p = Array(psk.utf8.prefix(63))
        buf.replaceSubrange(4..<(4 + s.count), with: s)
        buf.replaceSubrange(40..<(40 + p.count), with: p)
        defer { for i in buf.indices { buf[i] = 0 } }
        return buf.withUnsafeBytes { call(.join, input: $0) }
    }
}

// ---- keychain ------------------------------------------------------------------------------------------------------------
enum Keychain {
    static let service = "com.itlmt7925.wifi"
    static func query(_ ssid: String) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service, kSecAttrAccount as String: ssid]
    }
    static func get(_ ssid: String) -> String? {
        var q = query(ssid); q[kSecReturnData as String] = true; q[kSecMatchLimit as String] = kSecMatchLimitOne
        var out: CFTypeRef?
        guard SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess, let d = out as? Data else { return nil }
        return String(data: d, encoding: .utf8)
    }
    static func set(_ ssid: String, _ psk: String) {
        SecItemDelete(query(ssid) as CFDictionary)
        var q = query(ssid); q[kSecValueData as String] = Data(psk.utf8)
        q[kSecAttrLabel as String] = "Wi-Fi \(ssid) (itlmt7925)"
        SecItemAdd(q as CFDictionary, nil)
    }
    static func remove(_ ssid: String) { SecItemDelete(query(ssid) as CFDictionary) }
    static func known() -> [String] {
        var q: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service,
                                kSecReturnAttributes as String: true, kSecMatchLimit as String: kSecMatchLimitAll]
        var out: CFTypeRef?
        guard SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess, let a = out as? [[String: Any]] else { return [] }
        q.removeAll()
        return a.compactMap { $0[kSecAttrAccount as String] as? String }
    }
}

// ---- login item (a user LaunchAgent; no signing needed) ------------------------------------------------------------------
enum LoginItem {
    static let label = "com.itlmt7925.ItlWiFi"
    static var plist: URL { FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/LaunchAgents/\(label).plist") }
    static var enabled: Bool { FileManager.default.fileExists(atPath: plist.path) }
    static func set(_ on: Bool) {
        if !on { try? FileManager.default.removeItem(at: plist); return }
        let exe = Bundle.main.executablePath ?? CommandLine.arguments[0]
        let d: [String: Any] = ["Label": label, "ProgramArguments": [exe], "RunAtLoad": true, "ProcessType": "Interactive",
                                "LimitLoadToSessionType": "Aqua"]
        try? FileManager.default.createDirectory(at: plist.deletingLastPathComponent(), withIntermediateDirectories: true)
        (d as NSDictionary).write(to: plist, atomically: true)
    }
}

// ---- speed test ------------------------------------------------------------------------------------------------------------
// Runs /usr/bin/curl bound to the Wi-Fi interface (--interface), so a phone tether or Ethernet with a better route cannot
// take the traffic. Server: Cloudflare's speed test endpoints (speed.cloudflare.com/__down and /__up), the same ones its web
// test uses. Each direction runs for at most `secs` seconds; the rate is bytes moved / time, so a cut-off run still counts.
struct SpeedResult { var pingMs: Double?; var downMbps: Double?; var upMbps: Double?; var error: String?; var at = Date() }

enum SpeedTest {
    static let host = "https://speed.cloudflare.com"

    static func wifiInterface() -> String? {
        let svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("itlmt7925Eth"))
        guard svc != 0 else { return nil }
        defer { IOObjectRelease(svc) }
        return IORegistryEntrySearchCFProperty(svc, kIOServicePlane, "BSD Name" as CFString, kCFAllocatorDefault,
                                               IOOptionBits(kIORegistryIterateRecursively)) as? String
    }

    // curl ... -w "<size> <time> <connect>"; returns nil when curl moved nothing
    static func curl(_ iface: String, _ args: [String], stdin: URL? = nil) -> (bytes: Double, secs: Double, connect: Double)? {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/curl")
        p.arguments = ["-s", "-o", "/dev/null", "--interface", iface, "-w", "%{size_download} %{size_upload} %{time_total} %{time_connect} %{time_starttransfer}"] + args
        let out = Pipe(); p.standardOutput = out; p.standardError = FileHandle.nullDevice
        if let f = stdin { p.standardInput = try? FileHandle(forReadingFrom: f) }
        do { try p.run() } catch { return nil }
        p.waitUntilExit()                                    // exit 28 (time limit) is expected for the transfers
        let f = String(data: out.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8)?
            .split(separator: " ").compactMap { Double($0) } ?? []
        guard f.count == 5 else { return nil }
        let bytes = max(f[0], f[1]), xfer = f[2] - f[4]      // time after the first byte, so the handshake does not count
        return (bytes, xfer > 0.2 ? xfer : f[2], f[3])
    }

    static func run(secs: Int = 10) -> SpeedResult {
        var r = SpeedResult(pingMs: nil, downMbps: nil, upMbps: nil, error: nil)
        guard let iface = wifiInterface() else { r.error = "no Wi-Fi interface (is itlmt7925 connected?)"; return r }
        // latency: TCP connect time of a few empty requests, best of them
        let pings = (0..<4).compactMap { _ in curl(iface, ["--max-time", "3", "\(host)/__down?bytes=0"])?.connect }.filter { $0 > 0 }
        if let best = pings.min() { r.pingMs = best * 1000 } else { r.error = "speed.cloudflare.com not reachable over \(iface)"; return r }
        if let d = curl(iface, ["--max-time", "\(secs)", "\(host)/__down?bytes=50000000"]), d.bytes > 0 {
            r.downMbps = d.bytes * 8 / d.secs / 1e6 }
        // upload: a temporary file of random bytes (not compressible), streamed until the time limit
        let tmp = FileManager.default.temporaryDirectory.appendingPathComponent("itlwifi-up.bin")
        if !FileManager.default.fileExists(atPath: tmp.path) {
            var buf = [UInt8](repeating: 0, count: 64 << 20)
            _ = SecRandomCopyBytes(kSecRandomDefault, buf.count, &buf)
            FileManager.default.createFile(atPath: tmp.path, contents: Data(buf))
        }
        if let u = curl(iface, ["--max-time", "\(secs)", "-X", "POST", "-H", "Content-Type: application/octet-stream",
                                "--data-binary", "@\(tmp.path)", "\(host)/__up"]), u.bytes > 0 {
            r.upMbps = u.bytes * 8 / u.secs / 1e6 }
        if r.downMbps == nil && r.upMbps == nil { r.error = "no data moved" }
        return r
    }

    static func fmt(_ v: Double?) -> String {
        guard let v else { return "–" }
        return v >= 100 ? String(format: "%.0f", v) : v >= 10 ? String(format: "%.1f", v) : String(format: "%.2f", v)
    }
}

// ---- UI ------------------------------------------------------------------------------------------------------------------
final class App: NSObject, NSApplicationDelegate, NSMenuDelegate {
    let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
    let menu = NSMenu()
    var st = WifiStatus()
    var timer: Timer?
    var pending: (ssid: String, psk: String, remember: Bool)?   // saved to the keychain once the link is up
    var userOff = false
    var autoTried = Set<String>()
    var speed: SpeedResult?
    var speedRunning = false

    func applicationDidFinishLaunching(_ n: Notification) {
        menu.delegate = self
        item.menu = menu
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 3, repeats: true) { [weak self] _ in self?.refresh() }
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in self?.autoJoin() }
    }

    func refresh() {
        let old = st
        st = Driver.status()
        if let p = pending, st.state == "connected", st.ssid == p.ssid {
            if p.remember { Keychain.set(p.ssid, p.psk) }
            pending = nil
        } else if let p = pending, st.state == "failed", st.ssid == p.ssid, old.state != "failed" {
            pending = nil
            alert("Could not join “\(p.ssid)”", st.error.isEmpty ? "The connection failed." : st.error)
        }
        if (st.state == "idle" || st.state == "failed") && !userOff && pending == nil && st.scanGen != old.scanGen { autoJoin() }
        updateIcon()
    }

    func updateIcon() {
        guard let b = item.button else { return }
        var name = "wifi.slash", alpha: CGFloat = 1, value: Double = 1
        switch st.state {
        case "connected": name = "wifi"; value = st.rssi == 0 ? 1 : st.rssi >= -60 ? 1 : st.rssi >= -70 ? 0.66 : 0.33
        case "connecting", "scanning": name = "wifi"; alpha = 0.4
        case "failed": name = "wifi.exclamationmark"
        default: name = "wifi.slash"
        }
        let img = NSImage(systemSymbolName: name, variableValue: value, accessibilityDescription: "Wi-Fi")
        img?.isTemplate = true
        b.image = img
        b.alphaValue = alpha
        var tip = st.state == "connected" ? "\(st.ssid) · ch \(st.channel)" + (st.rssi == 0 ? "" : " · \(st.rssi) dBm") : st.state
        if let r = speed, r.error == nil { tip += " · ↓\(SpeedTest.fmt(r.downMbps)) ↑\(SpeedTest.fmt(r.upMbps)) Mb/s" }
        b.toolTip = tip
    }

    func menuNeedsUpdate(_ m: NSMenu) {
        st = Driver.status()
        m.removeAllItems()
        func label(_ t: String) { let i = NSMenuItem(title: t, action: nil, keyEquivalent: ""); i.isEnabled = false; m.addItem(i) }
        guard st.present else {
            label("Wi-Fi driver (itlmt7925) not loaded")
            m.addItem(.separator()); addFooter(m); return
        }
        switch st.state {
        case "connected":
            label("Wi-Fi: \(st.ssid)")
            label("Channel \(st.channel) · \(st.rssi == 0 ? "signal ?" : "\(st.rssi) dBm") · \(st.rate) Mb/s")
        case "connecting": label("Wi-Fi: connecting\(st.ssid.isEmpty ? "" : " to \(st.ssid)")…")
        case "scanning": label("Wi-Fi: scanning…")
        case "failed": label("Wi-Fi: not connected"); if !st.error.isEmpty { label(st.error) }
        case "off": label("Wi-Fi: off (boot-arg itlmt_m3=1 is not set)")
        default: label("Wi-Fi: not connected")
        }
        m.addItem(.separator())
        let known = Set(Keychain.known())
        if st.nets.isEmpty { label("No networks found yet") }
        for n in st.nets.prefix(20) {
            let i = NSMenuItem(title: n.ssid, action: n.joinable ? #selector(pick(_:)) : nil, keyEquivalent: "")
            i.target = self
            i.representedObject = n.ssid
            i.state = (st.state == "connected" && n.ssid == st.ssid) ? .on : .off
            let v = n.rssi == 0 ? 0.5 : n.rssi >= -60 ? 1 : n.rssi >= -70 ? 0.66 : 0.33
            i.image = NSImage(systemSymbolName: "wifi", variableValue: v, accessibilityDescription: nil)
            var sub = "ch \(n.channel)" + (n.rssi == 0 ? "" : " · \(n.rssi) dBm") + (n.locked ? " · 🔒 \(n.security)" : " · open")
            if known.contains(n.ssid) { sub += " · saved" }
            if !n.joinable { sub += " · not supported yet" }
            i.toolTip = sub
            if #available(macOS 14.4, *) { i.subtitle = sub }
            i.isEnabled = n.joinable
            m.addItem(i)
        }
        m.addItem(.separator())
        if speedRunning { label("Speed test running… (about 25 s)") }
        else if let r = speed {
            let t = DateFormatter.localizedString(from: r.at, dateStyle: .none, timeStyle: .short)
            label(r.error.map { "Speed test failed: \($0)" } ??
                  "↓ \(SpeedTest.fmt(r.downMbps)) Mb/s  ↑ \(SpeedTest.fmt(r.upMbps)) Mb/s  ·  \(SpeedTest.fmt(r.pingMs)) ms  (\(t))")
        }
        let sp = NSMenuItem(title: "Speed Test", action: speedRunning || st.state != "connected" ? nil : #selector(speedTest),
                            keyEquivalent: "t")
        sp.target = self; sp.isEnabled = !speedRunning && st.state == "connected"
        m.addItem(sp)
        m.addItem(withTitle: "Scan Now", action: #selector(scan), keyEquivalent: "r").target = self
        m.addItem(withTitle: "Other Network…", action: #selector(other), keyEquivalent: "").target = self
        if st.state == "connected" || st.state == "connecting" {
            m.addItem(withTitle: "Disconnect", action: #selector(disconnect), keyEquivalent: "").target = self
        }
        if !st.ssid.isEmpty && known.contains(st.ssid) {
            m.addItem(withTitle: "Forget “\(st.ssid)”", action: #selector(forget), keyEquivalent: "").target = self
        }
        m.addItem(.separator())
        addFooter(m)
    }

    func addFooter(_ m: NSMenu) {
        let li = NSMenuItem(title: "Open at Login", action: #selector(toggleLogin), keyEquivalent: "")
        li.target = self; li.state = LoginItem.enabled ? .on : .off
        m.addItem(li)
        m.addItem(withTitle: "Quit ItlWiFi", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
    }

    @objc func speedTest() {
        guard !speedRunning else { return }
        speedRunning = true
        item.button?.toolTip = "Speed test running…"
        DispatchQueue.global(qos: .userInitiated).async {
            let r = SpeedTest.run()
            DispatchQueue.main.async {
                self.speedRunning = false
                self.speed = r
                self.updateIcon()
                if let e = r.error { self.alert("Speed test failed", e); return }
                self.alert("Wi-Fi speed (\(self.st.ssid))",
                           "Download \(SpeedTest.fmt(r.downMbps)) Mb/s\nUpload \(SpeedTest.fmt(r.upMbps)) Mb/s\n" +
                           "Latency \(SpeedTest.fmt(r.pingMs)) ms\n\nLink: channel \(self.st.channel), PHY estimate \(self.st.rate) Mb/s. " +
                           "Measured against speed.cloudflare.com over the Wi-Fi interface only.")
            }
        }
    }

    @objc func scan() {
        let kr = Driver.call(.scan)
        if kr == kIOReturnBusy { return }                       // a scan ran less than 10 s ago; the list is fresh
        if kr != KERN_SUCCESS { alert("Scan failed", explain(kr)) }
    }

    @objc func pick(_ s: NSMenuItem) {
        guard let ssid = s.representedObject as? String else { return }
        if st.state == "connected" && st.ssid == ssid { return }
        if let psk = Keychain.get(ssid) { join(ssid, psk, remember: false); return }
        askAndJoin(ssid)
    }

    @objc func other() {
        let a = NSAlert(); a.messageText = "Join another network"
        a.informativeText = "WPA2-Personal networks only. Hidden networks are probed by name."
        let v = NSStackView(frame: NSRect(x: 0, y: 0, width: 260, height: 56)); v.orientation = .vertical; v.spacing = 6
        let n = NSTextField(frame: .zero); n.placeholderString = "Network name"
        let p = NSSecureTextField(frame: .zero); p.placeholderString = "Password"
        v.addArrangedSubview(n); v.addArrangedSubview(p)
        a.accessoryView = v; a.addButton(withTitle: "Join"); a.addButton(withTitle: "Cancel")
        a.window.initialFirstResponder = n
        NSApp.activate(ignoringOtherApps: true)
        guard a.runModal() == .alertFirstButtonReturn else { return }
        let ssid = n.stringValue
        guard (1...32).contains(ssid.utf8.count), (8...63).contains(p.stringValue.utf8.count) else {
            alert("Not joined", "The name must be 1-32 bytes and the password 8-63 characters."); return }
        join(ssid, p.stringValue, remember: true)
    }

    func askAndJoin(_ ssid: String) {
        let a = NSAlert(); a.messageText = "Join “\(ssid)”"
        a.informativeText = "Enter the network password."
        let v = NSStackView(frame: NSRect(x: 0, y: 0, width: 260, height: 50)); v.orientation = .vertical; v.alignment = .leading
        let p = NSSecureTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 22)); p.placeholderString = "Password"
        let r = NSButton(checkboxWithTitle: "Remember this network", target: nil, action: nil); r.state = .on
        v.addArrangedSubview(p); v.addArrangedSubview(r)
        a.accessoryView = v; a.addButton(withTitle: "Join"); a.addButton(withTitle: "Cancel")
        a.window.initialFirstResponder = p
        NSApp.activate(ignoringOtherApps: true)
        guard a.runModal() == .alertFirstButtonReturn else { return }
        guard (8...63).contains(p.stringValue.utf8.count) else { alert("Not joined", "The password must be 8-63 characters."); return }
        join(ssid, p.stringValue, remember: r.state == .on)
    }

    func join(_ ssid: String, _ psk: String, remember: Bool) {
        let kr = Driver.join(ssid: ssid, psk: psk)
        if kr != KERN_SUCCESS { alert("Could not join “\(ssid)”", explain(kr)); return }
        userOff = false
        pending = (ssid, psk, remember)
        refresh()
    }

    @objc func disconnect() {
        let kr = Driver.call(.disconnect)
        if kr != KERN_SUCCESS { alert("Disconnect failed", explain(kr)); return }
        userOff = true
        refresh()
    }

    @objc func forget() {
        Keychain.remove(st.ssid)
        if st.state == "connected" || st.state == "connecting" { disconnect() }
    }

    @objc func toggleLogin() { LoginItem.set(!LoginItem.enabled) }

    // At start and after scans while not connected: join the strongest saved network in range, once per network.
    func autoJoin() {
        guard st.present, !userOff, pending == nil, st.state == "idle" || st.state == "failed" else { return }
        let known = Set(Keychain.known())
        guard let n = st.nets.first(where: { known.contains($0.ssid) && $0.joinable && !autoTried.contains($0.ssid) }),
              let psk = Keychain.get(n.ssid) else { return }
        autoTried.insert(n.ssid)
        join(n.ssid, psk, remember: false)
    }

    func explain(_ kr: kern_return_t) -> String {
        switch kr {
        case kIOReturnNotPrivileged: return "Not allowed: only the user logged in at the console (or root) can change Wi-Fi."
        case kIOReturnNotReady: return "The driver is still starting (firmware, first scan). Try again in a moment."
        case kIOReturnNotFound: return "itlmt7925 is not loaded."
        case kIOReturnUnsupported: return "This itlmt7925 build has no runtime control. Install the newer kext."
        default: return String(format: "IOKit error 0x%08x", kr)
        }
    }

    func alert(_ title: String, _ text: String) {
        let a = NSAlert(); a.messageText = title; a.informativeText = text
        NSApp.activate(ignoringOtherApps: true); a.runModal()
    }
}

let app = NSApplication.shared
let delegate = App()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
