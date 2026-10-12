# Handoff: native Wi-Fi (`IO80211Controller`) work, cloud session -> local macOS (2026-10-12)

Branch **`wifi-native-80211`** (based on `main` 7635608, contains all of main; last commit `62649f3`). Continue on the Mac with
the installed macOS 15.8.1 (24H32, Darwin 24.6.0); the cloud session has no access to the kernel collection or the hardware.
No SMBIOS values, passphrases, MACs or neighbour SSIDs belong in this file or in `abi/`.

## Goal and ground rules

System Settings shows the MT7925 as **Wi-Fi** (menu bar, scan/join from the UI) instead of Ethernet: `itlmt7925` must expose an
`IO80211Controller` subclass. Plan, milestones and open questions: [NATIVE-WIFI.md](NATIVE-WIFI.md). Background: [WIFI-AS-WIFI.md](WIFI-AS-WIFI.md).

* **Clean room.** `itlwm` / `AirportItlwm` are GPL-2.0 (this repo is Clear BSD): do not read, copy or paraphrase them. Evidence comes
  from Apple's binaries (symbols, vtables, disassembly) via `tools/kc.py` and friends.
* **Behind a switch.** The proven data path stays untouched; the native class replaces `itlmt7925Eth` only with boot-arg
  `itlmt_native=1`. A panic on the native path must never cost a working machine; test on the installed system with the old kext on a
  stick as the way back.
* **Every ABI fact is per macOS build** (hash of the KC in `abi/native-wifi/24H32/source.json`). New macOS update = new dump.
* Read-only extraction: no `sudo`, no kext load, no NVRAM changes while collecting. Do not commit the `.kc`.

## What exists on the branch

| Path | What |
|---|---|
| `docs/NATIVE-WIFI.md` | plan, milestones, extraction commands, open questions, running status |
| `docs/IO80211-ABI-2.md` | extraction rounds 1-3 and analysis (controller `start` contract, interface/data-path registration, request routing, feasibility) |
| `abi/native-wifi/24H32/` and `round3/` | raw dumps: vtables, method lists, disassembly (~2.5 MB) |
| `tools/kc.py`, `kc_vtable.py` | read the KC: symbols, vtables, disassembly |
| `tools/collect_native_wifi_abi.py`, `dump_native_wifi_dispatch.py` | the collectors (refuse an existing output dir; the dispatch one only accepts the recorded KC hash) |
| `tools/annotate_vcalls.py` | names virtual calls (`call [reg+0xN]` -> slot) in a dump; the receiver class is not known, choose by what the object is |
| `tools/gen_io80211_header.py`, `check_io80211_header.py` | generate `kexts/itlmt7925/io80211/IO80211Controller.h` and **verify** it: clang's vtable layout of a concrete subclass equals Apple's (463 slots, 0 mismatches) |
| `kexts/itlmt7925/io80211/IO80211Controller.h` | generated, `sizeof == 0x128`; **not compiled into the kext yet** |

## Established (with evidence in the dumps)

* Instance sizes (MetaClass constructors): Controller 0x128, SkywalkInterface 0x118, InfraInterface 0x120, WorkQueue 0x50, CommandGate
  0x18, BCM core 0x130, BCM interface 0x130, IOSkywalkEthernetInterface 0x110, NetworkInterface 0xd0, PacketQueue 0xd8, PacketBufferPool
  0xb8, Tx submission 0x168, Rx completion 0x138, CCPipe/CCStream 0x90, CCLogStream 0x98, CCFaultReporter 0x90. The Controller's ivars
  pointer is at `this+0x120` (last 8 bytes), interfaces' at `+0x128`.
* `IO80211Controller::start` needs: a working `getWorkQueue()` (the subclass creates the queue **before** calling the base `start`),
  a **non-null `getLogger()`** (stored in a global without a null check, used at every log site), `CreatePostOffice()`, and it creates the
  command gate, timers, RNG agent and frame pool itself.
* Data path = IOSkywalkFamily packet queues and two `IOSkywalkPacketBufferPool`s registered via
  `IO80211InfraInterface::registerInfraEthernetInterface(RegistrationInfo*, IOSkywalkPacketQueue**, n, pool, pool)`; not `mbuf`s.
* Requests: `apple80211RouteInternal` indexes GET table `0xffffff80023e7770` / SET table `0xffffff80023e8900` by request ID (1..561). SET
  10 `SCAN_REQ`, GET 11 `SCAN_RESULT`, SET 20 `ASSOCIATE`, SET 22 `DISASSOCIATE`, SET 441 `WCL_SCAN_REQ`, SET 442 `WCL_ASSOCIATE`. The WCL
  wrappers call virtuals **on the interface** (slots 594/595 of the BCM interface = `AppleBCMWLANInfraProtocol::setWCL_*`), correcting
  round 2 which said controller. Card-specific codes `0x3f1..0x3f5` are a different namespace.
* `CCLogStream::withPipeAndName(pipe, name, opts)` can be called directly; it reads the log level (`CCStreamLogLevel`) at `opts+8`.

## Open (what blocks milestone 2, the stub that loads)

1. **Logger**: layouts of `CCPipeOptions` and `CCStreamOptions` (discriminator at `+0` selects the stream class), which pipe class
   `CCStream::init` expects, and what Apple's own code passes. Nothing may be guessed here.
2. **Interface side**: `RegistrationInfo` layout, how BCM creates the four Skywalk queues and two pools (sizes, flags), ownership at stop.
3. Headers for `IO80211WorkQueue`, `IO80211CommandGate`, `IO80211SkywalkInterface`, `IO80211InfraInterface`, `IOSkywalkNetworkInterface`,
   `IOSkywalkEthernetInterface` with the same generate-and-verify method (vtable dumps for all of them are in `round3/`).
4. Return types of virtuals are not in mangled names; the generator marks guesses. Only methods the stub overrides must be right.
5. Payload layouts of the `apple80211_*` structs (scan request/result, associate, capabilities); who owns the WPA2 4-way handshake on
   this macOS (decides whether `itl_wpa.cpp` is reused); the `OSBundleLibraries` needed (`com.apple.iokit.IO80211Family`, `IOSkywalkFamily`,
   `corecapture`) and their versions.

## Next steps, in order

1. Run extraction "round 4" (prompt below) -> `abi/native-wifi/24H32/round4/` + a section in `docs/IO80211-ABI-2.md`.
2. Generate and verify the interface-side headers; commit.
3. Milestone 2: stub class `itlmt7925Wifi : IO80211Controller` that registers, builds the work queue and logger, creates the interface, no radio,
   guarded by `itlmt_native=1`. Add `io80211/` to `scripts/build.sh` / `Info.plist` dependencies. Test on the installed system; success =
   loads, shows under the IO80211 plane, System Settings lists a Wi-Fi service, no panic on load/unload/sleep.
4. Milestones 3-7 in NATIVE-WIFI.md.

## Prompt for the local session (extraction round 4)

Use it verbatim (it is also the source of truth for what "round 4" means).

```text
Bối cảnh: repo ga403wm-hackintosh, nhánh `wifi-native-80211`. Đọc trước docs/HANDOFF-NATIVE-WIFI.md, docs/NATIVE-WIFI.md và
docs/IO80211-ABI-2.md ("Extraction round 3", "Analysis round 3"). macOS 15.8.1 / 24H32.

QUY TẮC: chỉ ĐỌC (không sudo, không nạp kext, không sửa NVRAM/EFI/mạng); ghi vào abi/native-wifi/24H32/round4/ và docs/.
Clean-room: không đọc/dán/diễn giải itlwm/AirportItlwm hay mã GPL; chỉ binary Apple qua tools/kc.py, kc_vtable.py,
annotate_vcalls.py. KC=/System/Library/KernelCollections/BootKernelExtensions.kc, SHA-256 phải là
cd013c14a7f21dbb8181fb06c44e079de8646b6a674abe21f6e47f42e8979af3 (khác thì dừng). Không commit .kc. Venv có capstone. Không đoán
layout: chỉ ghi điều có bằng chứng (offset, độ rộng, địa chỉ lệnh); chỗ chưa chắc ghi "chưa xác định". Không ghi SSID/MAC/serial.

VIỆC:
1. Logger: disassemble CCStream::initWithPipeAndName (0xffffff80031c0122), CCLogStream::initWithPipeAndName (0xffffff80031d0450),
   CCPipe::initWithOwnerNameCapacity, CCLogPipe::withOwnerNameCapacity + init, CCPipe::withOwnerNameCapacity. Ghi layout CCStreamOptions
   và CCPipeOptions (offset, độ rộng, trường bắt buộc, giá trị hợp lệ, discriminator +0 ứng với lớp nào), pipe được safeMetaCast sang
   lớp nào; và giá trị các trường options mà IO80211SkywalkInterface::start / AppleBCMWLANCore truyền thực tế.
2. RegistrationInfo + queue/pool: disassemble AppleBCMWLANSkywalkInterface::start (0xffffff800156aa90 và đoạn trước) phần điền
   IOSkywalkEthernetInterface::RegistrationInfo và tạo 4 queue (Tx/Rx submission/completion) + 2 IOSkywalkPacketBufferPool: factory,
   chữ ký, ý nghĩa đối số từ nơi gọi, kích thước/trường RegistrationInfo, tham số pool/queue (số lượng, buffer size, capacity, flags),
   ai sở hữu/giải phóng khi stop.
3. Header phía interface: tổng quát hoá tools/gen_io80211_header.py + check_io80211_header.py để sinh VÀ KIỂM TRA (clang
   -fdump-vtable-layouts, 0 mismatches) header cho IO80211WorkQueue (0x50), IO80211CommandGate (0x18), IO80211SkywalkInterface (0x118),
   IO80211InfraInterface (0x120), IOSkywalkNetworkInterface (0xd0), IOSkywalkEthernetInterface (0x110). Đặt trong
   kexts/itlmt7925/io80211/. Cần MacKernelSDK (https://github.com/acidanthera/MacKernelSDK) ngoài repo, truyền qua đối số 2 của
   check script. Báo trung thực lớp nào không kiểm tra được.
4. (nếu còn thời gian) kích thước payload và trường được đọc/ghi của SET 10/20/22/441/442 và GET 11/12/21 + getCARD_CAPABILITIES,
   từ ioctl-*.asm.txt: chỉ offset+độ rộng thấy trong disassembly.

TRƯỚC KHI COMMIT: chạy lại `python3 tools/check_io80211_header.py abi/native-wifi/24H32/round3` (0 mismatches) và các check mới;
git status không có .kc/SSID/MAC/serial; dung lượng round4 hợp lý. Commit lên wifi-native-80211, `git push origin wifi-native-80211`,
không tạo PR. In tóm tắt ≤ 20 dòng: đã xác minh / chưa / cái gì chặn stub kext.
```

## Housekeeping

* `main` is the only long-lived branch; `wifi-native-80211` is the working branch for this feature (merge into `main` only when the
  stub loads safely behind `itlmt_native`). `ccr-fc111298-ip8jp5`, `diag-esp-log`, `diag-mirror-m2m3` are retired.
* Run the generator/checker after any change: `python3 tools/check_io80211_header.py abi/native-wifi/24H32/round3` (needs `clang++`,
  `c++filt`, MacKernelSDK path via the 2nd argument or `$MACKERNELSDK`; the default path is only valid in the cloud sandbox).
