# TUF Dash F15 FX517ZM adaptation

This fork adds **TUF Control.app** and **TufWmi.kext** for the owner's Intel i7-12650H / RTX 3060 TUF Dash F15 FX517ZM. It does not replace the laptop's EFI or borrow the G14's AMD, MediaTek, OLED, or CPU policy drivers.

## Implemented

- Native SwiftUI window and menu item, battery percentage from macOS power sources.
- Firmware support detection and read-only CPU/GPU fan RPM from ASUS WMNB DSTS.
- Explicit Silent / Performance / Turbo requests; Turbo refused unless the AC adapter reports connected.
- Explicit 60/80/100% charge-limit requests. No automatic restore at login, wake, source changes or startup.
- Shows the profile/charge DSTS words as raw hex only, plus the last request accepted since driver start. These words are NOT a verified readback (profile read 0x10003, charge 0x90000), and a successful firmware return is not proof of effect.
- Root or console-UID administrator user client (see authorization below); exact selector/argument validation; rejects unrecognized device IDs and 64-bit narrowing attacks.

No fan curves, manual Intel power limits, AMD MSRs, GPU MUX writes, raw EC access, hotkey initialization, keyboard HID reports, or Slash lighting. The original G14 app remains in `macos/RogControl`; the Intel adaptation is separate in `macos/TufControl`.

## Evidence and remaining limits

The owner's ACPI dump from October 14 exposes `PNP0C14`, `_UID ATK`, WMNB, and the ASUS GUID `97845ED0-4E6D-11DE-8A39-0800200C9A66`. WMNB requires five dwords / 20 bytes. DSTS IDs 0x00110013/14 read the EC's FRMS/FRS2 and already convert to units of 100 RPM. Profile and charge IDs use the generic BIOS WCHK/W15H path; they MUST report present at runtime before the app permits a request. AC is read from ACPI0003._PSR.

The `TufModel=FX517ZM` personality is a manually selected target, **not independent model attestation** (SMBIOS is spoofed). The ASUS GUID is checked at driver start. Do not install this on another laptop. A new BIOS may behave differently. This is an experimental port; live support and writes are not yet hardware validated.

`tufwmi=1` starts a read-only bridge; `tufwmi=2` additionally permits DEVS requests (from 0.1.2; earlier builds enabled writes whenever loaded). No calls to INIT or DEVS happen at startup. Read selectors call only known DSTS IDs and _PSR. DEVS writes exist only behind the tufwmi=2 opt-in, a user request, authorization, support detection, allowed-value validation and strict firmware success return.

## Build and checks

```
bash scripts/build.sh TufWmi
bash scripts/build-tuf-control.sh
clang++ -std=c++17 -fsanitize=address,undefined test/tuf-policy-test.cpp -o build/tuf-policy-test
./build/tuf-policy-test
build/TufControl.app/Contents/MacOS/TufControl --status
```

Boundary tests cover unknown IDs, missing presence bits, Turbo off AC, supported charge presets and 64-bit truncation. Kext and app compile. App signature verification passes. App launches and returns battery status; no driver is expected on the current normal boot. Live kernel compatibility preflight must pass before staging. UI automation observation timed out; no visual screenshot QA was obtained.

## First boot test

The owner's working internal OpenCore and NVIDIA 2.5.5 stay unchanged. Use a separate rescue USB (set TUF_USB_VOLUME and TUF_USB_UUID for the staging script). `scripts/stage-tuf-usb.py` requires the matching successful kernel-preflight marker, backs up the rescue config, adds the opt-in test kext and boot argument to the USB only, preserves -nvoff and validates the config with OpenCore 1.0.8 ocvalidate. It refuses unknown volumes and existing test files.

After staging: restart, F8, choose UEFI Kingston, then Macintosh HD. Open TUF Control. First read `--status`; confirm fans are plausible and report which profile/charge capabilities are present. Do not describe controls as working until a live request and observation/readback have been tested. Avoid a stress benchmark during initial bring-up.

If boot fails, choose the unchanged internal OpenCore through F8 (or unplug the USB). Restore the original USB rescue copy with `python3 scripts/stage-tuf-usb.py --restore`; this checks the test config/kext hashes, archives the test kext, and restores the backed-up config. No internal-boot migration is performed.

## Installed app

`/Applications/TufControl.app`. No login agent or daemon was registered. Without the bridge, hardware buttons stay disabled and battery status remains available. The app performs no network requests or telemetry.

## Source and credits

Adapted from thanhnndev/ga403wm-hackintosh (AsusWmi WMNB transport and RogControl interaction design). New code remains BSD-3-Clause-Clear; see the original LICENSE and NOTICE.md. The original repository is not an endorsement of compatibility with this TUF.

## Local preparation result

Upstream baseline: `0b620b9c47e321adf73d1e0765c835b0095f7fb1`. Test collection preflight passed on macOS 15.8.1, retaining all four NVIDIA 2.5.5 drivers. The matching test kext and config are now staged on the identified USB; copied binary hash and config references verified. USB keeps -nvoff, while the internal EFI remains unchanged. The original rescue config and restore instructions are in Support/TUF-Control-Test. Real TUF-driver boot, fan readings, profile requests and charge-limit behavior are PENDING user reboot/observation.


## First hardware test: 0.1.0 access-check correction

User booted the USB with tufwmi=1. TufWmi 0.1.0 matched ATKD and registered normally, but the app was denied with 0xe00002c1. Apple's kIOClientPrivilegeAdministrator means UID 0; it does not include ordinary members of the admin group. The first app's admin-account error and "bridge not active" label were misleading.

A root-only read probe succeeded: caps=15, raw profile=65539 (present, reported value 3, unknown mapping), charge=589824 (present, no useful limit readback), CPU fan=65574 (~3800 RPM), GPU fan=65575 (~3900 RPM). No DEVS writes were performed. Firmware support presence is demonstrated; profile effects and charge-limit behavior are still untested.

0.1.1 permits UID 0 or an active-console member of the admin group (GID 80). It rejects non-admin console users and remote/background admin sessions. The group check uses the opening task's current kauth credentials in IOServiceOpen. Host boundary tests cover this authorization matrix. Access-denied UI now distinguishes a loaded inaccessible driver from an absent bridge and no longer labels unsupported firmware before connecting.

0.1.1 rebuilt, authorization matrix tests passed, disposable kernel collection passed with new kauth symbols. Installed app updated; corrected driver staged only on USB, old 0.1.0 archived under Support/TUF-Control-Test/versions/0.1.0. Rescue backup and internal EFI untouched. Loaded kernel driver remains 0.1.0 until reboot. No profile/charge writes have been tested or performed.

## 0.1.2 review corrections (source and build/ only; not staged)

An independent review of 0.1.1 found the following. They are corrected in source and in `build/`, but nothing is staged on the USB yet. The USB still holds 0.1.1 with `tufwmi=1`, which under 0.1.1 means writes ARE enabled.

- **Authorization claim was wrong.** The `console` IOKit privilege is UID-based. Any process running as the logged-in console admin passes it, including that user's SSH sessions, LaunchAgents and scripts, without a password prompt. 0.1.0/0.1.1 did not reject remote/background sessions of the same user. Policy is unchanged (accepted risk: only allowlisted values are writable), and the claim is corrected here.
- **Mixed subjects.** Root/console checks used the caller-chosen `owningTask` while the admin-group check used the calling thread's credentials. 0.1.2 rejects `owningTask != current_task()`.
- **Writes were not gated.** `TufAllowWrites` was hard-coded true in Info.plist and has been removed. Writes now require `tufwmi=2`.
- **False readback risk.** The app decoded the low 16 bits of the profile/charge DSTS words as the current setting. It now shows raw hex and the last accepted request only.
- **AC adapter looked up once at start.** It is now retried lazily, so an early-boot miss no longer leaves AC unknown (which blocked Turbo) for the whole boot. `--status` now includes `acPower` and the driver-reported ABI.
- Wrong user-client type returns kIOReturnBadArgument instead of NotPrivileged. Unused IOHIDFamily dependency removed. `start()` failure paths call `IOService::stop`. Refreshing no longer disables the buttons, and the menu bar refreshes every 30 s in the background.

Not yet done for 0.1.2: root kernel-collection preflight (`scripts/preflight-tuf.sh`) with the new `current_task` import, and USB staging. The staging script refuses to overwrite an existing test kext, so updating the USB is a manual, reviewed step. Recommended first boot: 0.1.2 with `tufwmi=1` (read-only), confirm the app connects without sudo, then `tufwmi=2` and test one write at a time with a physical observation.

## App 0.2.0: RogControl design

TufControl now uses RogControl's layout (docs/rog-control-design.md): a menu bar popover with mode tiles and charge-limit buttons, plus a sidebar window (Overview, Performance, Battery, Settings). `--background` starts it in the menu bar only. "Open at Login" is an opt-in user LaunchAgent (`local.tuf.fx517zm.control`). Unlike RogControl, it never re-applies a mode or limit at launch, login, wake or a power-source change, and it has no fan curves, manual power limits, Slash/keyboard lighting or AMD sensors. The highlighted mode/limit is the driver's last accepted request, not a firmware readback. Build/install: `bash scripts/build-tuf-control.sh --install`. Offscreen light/dark renders were checked against the live driver (0.1.1, USB boot with NVIDIA); the hardware buttons were not pressed during this check.

## 0.2.0 driver / 0.3.0 app: full RogControl feature port (built, NOT yet booted)

Every ID below was checked in the owner's FX517ZM DSDT (`hey-x20/work/brightness/dsl`):

| Feature | Source | Access |
|---|---|---|
| CPU temperature | DSTS 0x00120094 → EC CTMP | read |
| GPU temperature | `EC0.SEN3._TMP` → EC VRTT (SSDT-15's NVIDIA platform table uses it as the GPU temperature) | read; 3000 dK = "EC not ready", ignored |
| Package power | MSR 0x611/0x606 via `rdmsr_carefully`, gated on the real CPUID (0x906A3, Alder Lake; macOS shows a spoofed 0xA5) | read |
| Fan curves | DSTS 0x00110024/25 arg 0/1/2 = Performance/Silent/Turbo (CFCV/CSFV/CTFV) | read; DEVS → SFTC/SFTG, CPU write sets EC FMAE |
| PL1 / PL2 | DEVS 0x001200A3 → EC PL1V, 0x001200A0 → PL2V (FPPT 0x001200C1 unsupported) | write, 15–65 / 25–95 W, only while custom fans are active |
| Keyboard brightness / TUF RGB | 0x00050021 / 0x00100056 are not in the DSDT (SMI path) | probed once at runtime; hidden if absent; RGB uses 0xb3 (not saved to BIOS) |
| GPU MUX / dGPU | DSTS 0x00090016 / 0x00090020 | read only, never written |
| NVIDIA Dynamic Boost / TGP | 0x001200C0 / C2 | **not used** (would notify NPCF/PEGP under NullMoth) |

Manual mode follows RogControl: request Performance, copy the chosen firmware curve to both fans (sets FMAE), then PL1 ≤ PL2. Any failure requests the profile again. **Risk:** nothing in ACPI clears FMAE; returning to automatic fans relies on the EC resetting it on a profile request, as Linux asus-wmi assumes. Verify this on the first Manual test ("Automatic fans" button, then watch the fans follow load again).

SMI-backed probes (profile, charge, keyboard, MUX) run once on the first request, so refreshes only poll EC fields and the MSR. ABI 2 state is 24 words; ABI 1 clients still get their 10 words. The app reads both ABIs. With 0.2.0, `tufwmi=1` is read-only and **`tufwmi=2` is required for any button**, unlike the 0.1.1 currently on the USB.

Host checks: policy tests (curves, PL gating, RGB/keyboard ranges, never-writable IDs, RAPL model gate) pass; kext builds without warnings; app tested read-only against the loaded 0.1.1 driver and rendered offscreen with mock ABI 2 data. Pending: root kernel-collection preflight (new `rdmsr_carefully` / `com.apple.kpi.unsupported` link), USB staging, and every hardware test.

## Internal OpenCore (2026-10-26)

At the owner's request TufWmi 0.2.2 is now in the INTERNAL OpenCore (disk0s3): one Kernel.Add entry (same as the USB's) and `tufwmi=2` appended to boot-args; nothing else changed (diff-checked, ocvalidate clean, kernel-collection preflight passed). Backups: `TUF-Control-Backup/config.before-tufwmi.plist` on that ESP and `backups/internal-config.before-tufwmi.plist` here. To undo: mount disk0s3, copy that backup over `EFI/OC/config.plist` (the kext can stay; it is inert without its Kernel.Add entry). The Kingston USB remains a separate rescue (0.2.1, tufwmi=2, NVIDIA on; `stage-tuf-usb.py --restore` returns its original -nvoff config).

0.2.2: GPU temperature tries SEN3._TMP (device, then EC-relative), then the EC byte VRTT, then its absolute path; per-attempt IOReturn codes in the TufGpuTempDiag registry property. App 0.3.1: opt-in "Restore charge limit at login" (default on, enables Open at Login once) re-requests the last accepted 60/80 % limit when the driver has no charge request (i.e. after a reboot); nothing else is ever re-applied automatically.

Faster internal boot (same day, owner's request): internal config only, Misc.Boot.Timeout 15 → 5, Misc.Debug.Target 67 → 0 (no opencore-*.txt log file per boot), AppleDebug → false. ocvalidate clean, diff-checked. Backup: `TUF-Control-Backup/config.before-fastboot.plist` (ESP) and `backups/internal-config.before-fastboot.plist`. OpenCore.efi is still the DEBUG build (no local RELEASE copy). The USB keeps full debugging as the fallback.

## HiDPI (2026-10-26)

Panel: DisplayVendorID 0x0dae, DisplayProductID 0x153c, 1920x1080 @ 144 Hz. An app-only switch to the scaled 1344x756 mode showed a (blurry) picture: NullMoth 2.5.5 does scaled mode sets on this laptop, unlike NullMoth 1.13 on the G14. Override installed at `/Library/Displays/Contents/Resources/Overrides/DisplayVendorID-dae/DisplayProductID-153c` (copy in `backups/`): scale-resolutions 3360x1890, 3200x1800, 2880x1620 (looks like 1680x945 ≈ 16" MacBook Pro text size, 1600x900, 1440x810), entry flags 1 / 0x800000. Takes effect after a restart. Test with `build/hidpi-pick test 1680` (app-only, restores itself), then `build/hidpi-pick set 1680` (reverts unless Return within 15 s). Undo: delete that override file and restart.

HiDPI follow-up: after the reboot no scaled HiDPI modes appeared. Cause: NVRMFB publishes `IOFBScalerInfo` with max 1920x1080 (= raster), so IOGraphicsLib drops the 3360x1890 / 3200x1800 / 2880x1620 scale-resolutions entries. NVRMFB 2.5.5's boot-arg `nvfbhidpi` (any non-zero value; disassembly: max = min(2 × raster, 7680x4320), also bounded by scanout memory) raises it to 3840x2160. Added `nvfbhidpi=1` to the INTERNAL config only (backup `config.before-nvfbhidpi.plist`); the USB keeps the old boot-args as fallback.

Result (same day, owner): with nvfbhidpi=1 macOS lists looks-like 1680x945 / 1600x900 / 1440x810 HiDPI at 144 Hz and 60 Hz; the app-only test of 1680x945 switched seamlessly and looked sharper. The owner picks the size in System Settings → Displays. Undo: remove nvfbhidpi=1 (or restore `config.before-nvfbhidpi.plist`) and/or delete the override file.
