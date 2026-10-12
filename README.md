# ga403wm-hackintosh

**macOS on the ASUS ROG Zephyrus G14 (GA403WM, 2025): Ryzen AI 9 HX 370 "Strix Point" + RTX 5060 Laptop.**
OpenCore EFI, kernel extensions, menu bar apps and research notes. Tiếng Việt: [README.vi.md](README.vi.md).

<p align="center">
  <img src="docs/images/hero.svg" alt="Screenshot placeholder: macOS desktop on the GA403WM" width="720">
</p>

> ## ⚠️ This is a vibe-coded project. Read before you touch your laptop.
>
> **Almost all of the code and documentation in this repository was written by an AI (Claude, by Anthropic)**, steered and
> tested by one person on **one** GA403WM. Nobody has audited it. In particular:
>
> * The kexts (`itlmt7925`, `AsusWmi`, `AmdCppc`, `MtkBtFw`, ...) run **inside the macOS kernel**. A bug is a kernel panic, a
>   hang, or at worst corrupted data. `AsusWmi` and `AmdCppc` write to the embedded controller, the BIOS (through SMI) and CPU
>   registers.
> * The Wi-Fi driver is written from scratch and stores your Wi-Fi password **in plain text** inside the kext.
> * The EFI and boot-args are copied from other projects and bent until this one machine booted. They are not hardened
>   (SIP is mostly off, AMFI is relaxed, Secure Boot is off).
> * Things work **on the reference unit**. Your unit (other BIOS, other panel, other SKU) may not.
> * **No warranty.** If you brick, panic, lose data or void your warranty, that is on you. See [LICENSE](LICENSE).
>
> Back up everything. Keep Windows/Linux recovery media. Do not install this on a machine you cannot afford to reinstall.
> Do not use it for work you cannot lose. Treat it like a research project, not a product.

## Status in one minute

| | |
|---|---|
| Supported macOS | **macOS 15 Sequoia** only (developed on 15.6.1, running daily on **15.8.1**, build 24H32) |
| **macOS 26 Tahoe** | **Not supported, not tested.** We stay on 15 until it is stable. The NVIDIA driver we depend on is macOS 15 only. Do not upgrade. |
| Machine | ASUS ROG Zephyrus G14 **GA403WM**, BIOS GA403WM.310, 2880x1800 OLED. Other GA403 variants are untested |
| Boot | OpenCore 1.0.9 + a Zen 5 patch, from the internal ESP; Windows/Linux stay in the OpenCore picker |
| Graphics | RTX 5060 through the third-party [NullMoth driver](https://github.com/nullmoth/nvidia-macos-driver); **GPU mode must be "Ultimate" (MUX)**, the iGPU is not usable |
| Wi-Fi | Own driver, shows up as **Ethernet** (en*), slow; no native Wi-Fi menu yet. You must **inject your SSID/password** ([guide](docs/INSTALL.md#4-wi-fi-inject-your-network)) |
| Known broken | I2C touchpad (dead on the reference unit, cause unknown), BLE mouse (fix untested). HiDPI works for one size only |

Legend used everywhere: ✅ verified on the reference unit · 🟡 implemented, needs more testing · 🔧 in progress ·
❌ not working · ⚪ not started / not verified.

## What works

| Area | State | Notes | Details |
|---|---|---|---|
| Boot macOS 15 from the internal disk | ✅ | OpenCore 1.0.9 + `fix-zen5-fsb.diff` (timers fired ~10x early on Zen 5) | [efi/README.md](efi/README.md) |
| CPU (12 cores / 24 threads) | ✅ | `AmdCppc` + `AmdCppcPolicy` enable CPPC; Zen 5 cores reach ~4.9 GHz under load (were stuck at 2 GHz) | [docs/CPU-POWER.md](docs/CPU-POWER.md) |
| NVIDIA RTX 5060 + 2880x1800 OLED @ 120 Hz | ✅ | Third-party NullMoth driver 1.14.2 + two local patches. Needs MUX = dGPU only. External monitor works | [docs/NULLMOTH.md](docs/NULLMOTH.md) |
| USB keyboard / mouse / webcam | ✅ | `ItlXhciKick` fixes the 2 s key-release lag; USB port map is in the reference EFI | [docs/USB-XHCI-LAG.md](docs/USB-XHCI-LAG.md) |
| Wi-Fi MT7925 | 🟡 | WPA2-PSK, DHCP, HE/160 MHz link. Measured only **~5-40 Mb/s**; no WPA3, no 6 GHz; appears as Ethernet | [docs/WIFI.md](docs/WIFI.md) |
| Bluetooth MT7925 | 🟡 | `MtkBtFw` loads the firmware; classic devices (headphones) connect. BLE mouse: fix written, untested | [docs/BLUETOOTH.md](docs/BLUETOOTH.md) |
| ROG performance profiles, fans, charge limit | 🟡 | `AsusWmi` + RogControl app. Silent/Performance/Turbo change real clocks and fan speed | [docs/ASUSCTL-MACOS.md](docs/ASUSCTL-MACOS.md) |
| Keyboard backlight, Slash lighting | 🟡 | Via `AsusWmi` (HID reports from the kernel) | [docs/ASUSCTL-MACOS.md](docs/ASUSCTL-MACOS.md) |
| OLED brightness | 🔧 | Panel is AUX-only; `NVRMEdpBL` (DPCD) installed, verification pending. Software dimming overlay as fallback | [docs/OLED-BRIGHTNESS-DPCD.md](docs/OLED-BRIGHTNESS-DPCD.md) |
| Sleep / wake | 🟡 | A sleep/wake cycle restores CPU policy; not tested broadly | |
| Dual boot Windows / Linux | ✅ | Chosen from the OpenCore picker; repair notes for lost UEFI entries | [docs/DUAL-BOOT.md](docs/DUAL-BOOT.md) |
| Touchpad (I2C `093a:3011`) | ❌ | Dead on the reference unit (Windows too) since 2026-10-10; cause not found. A USB mouse works | [HANDOFF #1](docs/HANDOFF.md) |
| HiDPI "looks like 1440 x 900" @ 120 Hz | 🟡 | Works on the reference unit with a display override; **other scales go black** (the driver cannot do scaled modes) | [docs/SCRIPTS.md §4](docs/SCRIPTS.md#4-hidpi-looks-like-1440-x-900-step-by-step) |
| Audio, SD-card reader, battery %, Fn keys other than brightness | ⚪ | Kexts are in the EFI (AppleALC layout 33, Sinetek-rtsx, SMCBatteryManager) but **not verified** | |
| Native Wi-Fi in System Settings / AirDrop / Continuity | ⚪ | Needs an `IO80211Controller` port; research only | [docs/WIFI-AS-WIFI.md](docs/WIFI-AS-WIFI.md) |
| iGPU Radeon 890M | ❌ | Hidden from macOS on purpose; no driver | |

Everything in this table is dated and sourced in [docs/HANDOFF.md](docs/HANDOFF.md) (the lab notebook).

## Screenshots

Placeholders; real screenshots will replace them. See [docs/images/README.md](docs/images/README.md) for what is wanted.

| RogControl popover | RogControl window | ItlWiFi menu |
|---|---|---|
| ![RogControl popover (placeholder)](docs/images/rogcontrol-popover.svg) | ![RogControl window (placeholder)](docs/images/rogcontrol-window.svg) | ![ItlWiFi (placeholder)](docs/images/itlwifi.svg) |

| OpenCore picker | About This Mac | Fan curves |
|---|---|---|
| ![OpenCore picker (placeholder)](docs/images/opencore-picker.svg) | ![About This Mac (placeholder)](docs/images/about-this-mac.svg) | ![Fan curves (placeholder)](docs/images/fan-curves.svg) |

## Install

Full guide: **[docs/INSTALL.md](docs/INSTALL.md)** (Tiếng Việt: [docs/INSTALL.vi.md](docs/INSTALL.vi.md)). Short version:

1. BIOS: MUX = **Ultimate**, Above 4G Decoding **ON**, CSM **OFF**, Secure Boot **OFF**. Back up the disk.
2. Download the **prebuilt [release v1](https://github.com/thanhnndev/ga403wm-hackintosh/releases)** (kexts, apps, CPU daemon, patched OpenCore, reference EFI, checksums;
   or build with `bash scripts/build.sh` on a Mac) and the third-party kexts of [efi/KEXTS.md](efi/KEXTS.md). How to use the release and
   every script: **[docs/SCRIPTS.md](docs/SCRIPTS.md)** (Tiếng Việt: [SCRIPTS.vi.md](docs/SCRIPTS.vi.md)).
3. Assemble the EFI from [efi/](efi/README.md), generate **your own SMBIOS**, add the patched OpenCore.
4. **Inject your Wi-Fi network** into `itlmt7925.kext` (there is no native Wi-Fi UI yet) or use USB tethering.
5. Install macOS 15 (offline installer recommended: the online installer is 16 GB and our Wi-Fi is slow), then install the
   NVIDIA driver, the apps and the policy daemon.

## Components

Every kext and app, what it does and how far along it is: **[docs/COMPONENTS.md](docs/COMPONENTS.md)**.

| Component | One line | State |
|---|---|---|
| `itlmt7925.kext` | MT7925 Wi-Fi driver | 🟡 works, slow, shows as Ethernet |
| `ItlXhciKick.kext` | USB lag fix for AMD xHCI | ✅ |
| `AsusWmi.kext` | ASUS WMI/EC/HID: profiles, fans, charge limit, backlight, Slash | 🟡 |
| `AmdCppc.kext` + `AmdCppcPolicy` | CPU boost/EPP control + daemon | ✅ / 🟡 |
| `MtkBtFw.kext` | MT7925 Bluetooth firmware loader | 🟡 |
| `BtLeDlFix.kext` | Lilu plugin for BLE mice | 🟡 untested |
| `NVRMEdpBL.kext` (in a NullMoth fork) | OLED brightness over eDP AUX | 🔧 |
| `ItlDiag.kext` | boot diagnostics | debug tool |
| **ROG Control** (`RogControl.app`) | menu bar + window: profiles, fans, charge, lighting, dimming | 🟡 |
| **ItlWiFi** (`ItlWiFi.app`) | menu bar Wi-Fi: scan, join, speed test | 🟡 |
| **TapGuard** (`TapGuard.app`) | ignore touchpad taps while typing | 🟡 (touchpad itself is ❌) |

## Roadmap

**[docs/ROADMAP.md](docs/ROADMAP.md)** lists every workstream with its state, branch and next step. Headlines:

* 🔧 Wi-Fi throughput, then a **native Wi-Fi** (`IO80211Controller`) so the menu bar and System Settings work
* 🔧 OLED brightness via DPCD, desktop smoothness (WindowServer CPU, VRAM clear)
* ❌ → ⚪ touchpad, BLE mouse; HiDPI other than 1440 x 900
* ⚪ audio / SD / battery verification, first tagged release with CI-built kexts
* ⚪ **Codebase standardisation** (shared helpers, cleanup of debug code, scripts): component by component, only after each is proven stable
* ⚪ **macOS 26**: only after 15 is stable. Not planned before that.

## Repository map

```
efi/                 sanitized OpenCore config.plist + SSDTs, third-party kext list (KEXTS.md, kexts.json)
kexts/               itlmt7925, ItlXhciKick, AsusWmi, AmdCppc, MtkBtFw, BtLeDlFix, ItlDiag
macos/               RogControl, ItlWiFi, TapGuard (Swift apps), AmdCppcPolicy (launch daemon)
fw/                  MediaTek MT7925 firmware (linux-firmware), with its license
patches/opencore/    OpenCorePkg patch for Zen 5
scripts/             build, install, Wi-Fi credential injection, log collection, EFI export + privacy check
tools/               kernel-collection reader, HiDPI/frame-pacing/CPU-clock probes
test/                host tests (WPA2, capabilities, Block Ack, ...), run in CI
docs/                install guide, scripts guide (incl. HiDPI), components, roadmap, per-topic research notes, HANDOFF (lab notebook)
```

## Privacy

* The repository never contains Wi-Fi passphrases or SMBIOS identifiers: `Info.plist` ships with empty `itl_ssid*/itl_psk*`
  keys, `efi/OC/config.plist` carries `CHANGE_ME_*` placeholders, and CI fails if real values appear. A kext **you** inject
  credentials into holds them in plain text: never share it.
* Logs contain your card's MAC address and nearby SSIDs/BSSIDs. Redact them before posting an issue.

## Contributing, credits, license

* Reports from other GA403WM owners are the most valuable thing: open an issue with the **Hardware report** template and
  read [CONTRIBUTING.md](CONTRIBUTING.md).
* This project stands on others' work: [NullMoth](https://github.com/nullmoth/1401) (EFI base and NVIDIA driver), Acidanthera
  (OpenCore, Lilu, VirtualSMC, AppleALC, ...), the Linux `mt76`, `btmtk`, `asus-wmi` and `asusctl` projects (design
  references). Full list and licenses: [NOTICE.md](NOTICE.md).
* Code: [Clear BSD License](LICENSE) (re-implements parts of Linux `mt76`, BSD-3-Clause-Clear, © MediaTek). Firmware in `fw/`:
  [MediaTek's license](fw/LICENCE.mediatek). `efi/` derives from `nullmoth/1401` (PolyForm Noncommercial 1.0.0) and is **not**
  Clear BSD: noncommercial use only.
* Not affiliated with ASUS, Apple, NVIDIA, MediaTek, AMD or NullMoth. Apple, macOS, ROG and the rest are trademarks of their owners.
  You are responsible for complying with Apple's license terms for macOS.
