# Roadmap and work tracking

Last updated: 2026-10-12. This page is the single place that says **what is being worked on, where, and what is next**.
The day-by-day lab notebook (evidence, failed attempts) is [HANDOFF.md](HANDOFF.md); per-topic research is in the other files
of this folder. Nothing here is a promise: this is a one-person, AI-assisted hobby project.

States: ✅ done · 🔧 in progress (someone is on it now) · 🟡 implemented, awaiting verification · ❌ broken / blocked ·
⚪ not started · 🧊 parked.

## Where we are

* **Alpha, reference unit only.** macOS 15.8.1 (24H32) runs daily from the internal disk with OpenCore 1.0.9, NVIDIA RTX 5060 and
  the 120 Hz OLED through the NullMoth driver, Wi-Fi/Bluetooth/USB through our kexts, ROG controls through `AsusWmi` +
  RogControl.
* **macOS 15 only.** macOS 26 (Tahoe) is explicitly **out of scope** until macOS 15 is stable for other GA403WM owners
  (see [milestones](#milestones)). The NVIDIA driver is macOS 15 only anyway.

## Milestones

| | Goal | Gate (all must be true) | State |
|---|---|---|---|
| **M0** | Alpha: boots and is usable on the author's unit | internal-disk install, GPU, USB, Wi-Fi, BT, ROG controls | ✅ |
| **M1** | **Stable macOS 15 for GA403WM** (public beta) | a second owner reproduces the install from this repo; touchpad works; published USB map; clean (non-debug) EFI; OLED brightness verified; sleep/wake verified; Wi-Fi at a usable speed (>= 100 Mb/s); first tagged release with CI-built kexts and apps | 🔧 |
| **M2** | Native Wi-Fi | `IO80211Controller` port: Wi-Fi menu, System Settings pane, no credentials in the kext | ⚪ research |
| **M3** | Evaluate macOS 26 | only after M1 holds for a few weeks on 15.x; needs a NVIDIA driver and OpenCore support for 26 | 🧊 not planned yet |

## Workstreams

"Where" = where the work lives. Everything in this repository is developed on `main` through short-lived
`track/<name>` branches and pull requests; NullMoth driver work lives in a fork and is sent upstream first.

| # | Workstream | State | Where | Doing now / next step | Doc |
|---|---|---|---|---|---|
| 1 | **Install experience** (README, INSTALL, release assets, issue templates) | 🔧 | `main` | this page, publish first release (kexts + apps zip) | [INSTALL.md](INSTALL.md) |
| 2 | **EFI hygiene**: published USB map, drop debug boot-args / `IOPCIMSIMode` leftovers / ItlDiag, verbose off | ⚪ | `main` (`efi/`) | USBToolBox map for the GA403WM replacing the `UTBDefault` placeholder, keep keyboard `0b05:19b6` internal | [efi/README.md](../efi/README.md) |
| 3 | **Touchpad** (I2C-HID `ASUP1208`, `093a:3011`) | ❌ | `main` | dead on the reference unit in Windows too since 2026-10-10. Next: full power drain (unplug AC, hold power 40 s), re-check Windows; if it returns, bisect `itlmt7925` / VoodooI2C / VoodooGPIO | [HANDOFF #1](HANDOFF.md) |
| 4 | **Wi-Fi throughput** (`itlmt7925`) | 🔧 | `main` (`kexts/itlmt7925`) | measured ~8 Mb/s down / 4-6 up at HE 160 MHz (PHY ~2.4 Gb/s): RX reorder timeouts and 30 % TX retries. Next: RX ring drops, TX retry rate, LAN `iperf3` baseline | [WIFI.md](WIFI.md) |
| 5 | **Wi-Fi app** (ItlWiFi) and runtime control | 🟡 | `main` (`macos/ItlWiFi`) | scan/join/reconnect/speed test; hardening, keychain-free storage | [WIFI.md](WIFI.md#runtime-control-itlwifi-menu-bar-app) |
| 6 | **Native Wi-Fi** (`IO80211Controller` subclass, Skywalk) | ⚪ | research notes only | ABI step 1 done (12 pure virtuals); Skywalk interface and `apple80211_*` structs not mapped. Clean-room, no GPL `itlwm` code | [WIFI-AS-WIFI.md](WIFI-AS-WIFI.md), [IO80211-ABI.md](IO80211-ABI.md) |
| 7 | **Wi-Fi gaps**: WPA3-SAE, open networks, 6 GHz, power save, regulatory domain, MSI instead of polling | ⚪ | `main` | after throughput | [WIFI.md](WIFI.md) |
| 8 | **Bluetooth** (`MtkBtFw`, `BtLeDlFix`) | 🟡 | `main` | reboot-test `BtLeDlFix` with a BLE mouse; treat "no answer to the first PATCH_DWNLD query" on warm reboot as success; pair keyboard/headphones | [BLUETOOTH.md](BLUETOOTH.md) |
| 9 | **ROG Control** (`AsusWmi`, `RogControl`) | 🟡 | `main` (`kexts/AsusWmi`, `macos/RogControl`) | verify charge limit and fan-curve writes, Aura effects beyond brightness, panel overdrive (absent in this BIOS), GPU MUX read-only | [ASUSCTL-MACOS.md](ASUSCTL-MACOS.md), [rog-control-design.md](rog-control-design.md) |
| 10 | **CPU power** (`AmdCppc`, `AmdCppcPolicy`) | 🟡 | `main` | one live round OK (~4.9 GHz all-core). Next: soak over days, sleep/wake loops, battery-life comparison, upstreamable kext cleanup | [CPU-POWER.md](CPU-POWER.md) |
| 11 | **OLED brightness** (`NVRMEdpBL`, DPCD AUX) | 🔧 | NullMoth fork, branch `nvrm-edp-aux-backlight` | verify after reboot per checklist, then switch the software dimmer off; upstream PR | [OLED-BRIGHTNESS-DPCD.md](OLED-BRIGHTNESS-DPCD.md) |
| 12 | **Desktop smoothness**: VRAM clear, WindowServer CPU, PAT | 🔧 | NullMoth fork, branch `nvrmfb-vram-clear-nontemporal` + `main` docs | VRAM clear now ~1 GB/s with the PAT fix (was ~8 MB / 0.7-1.1 s); WindowServer 33 % of one core in the test; upstream PR | [VRAM-GRANT-STALL.md](VRAM-GRANT-STALL.md), [WINDOWSERVER-CPU.md](WINDOWSERVER-CPU.md), [WINDOWSERVER-RESEARCH.md](WINDOWSERVER-RESEARCH.md) |
| 13 | **HiDPI / scaled modes** | ❌ | `main` (`tools/hidpi-try.swift`) | WindowServer refuses the density-2 copy of the native mode (`Mode 65792 not available`); blocked on the NVIDIA driver's mode flags | [HANDOFF #7](HANDOFF.md) |
| 14 | **Black screen / flicker with 120 Hz + modeset** | 🟡 | NullMoth upstream | stable on 1.13-1.14 with the boot raster at 120 Hz; do not change modes at login | [HANDOFF #2](HANDOFF.md), [NULLMOTH.md](NULLMOTH.md) |
| 15 | **Boot time** (~36 s kernel to login) | 🟡 | `main` (EFI) | OpenCore debug target off, `-nvrmnobootscreen`, verbose off; remaining: GOP console cost | [HANDOFF #9](HANDOFF.md) |
| 16 | **Unverified hardware**: audio, SD reader, battery %, webcam, sleep/wake, lid, Fn keys | ⚪ | - | go through them one by one; each result into [HANDOFF.md](HANDOFF.md) and the README table | [README](../README.md#what-works) |
| 17 | **Release engineering**: tag `v0.x`, CI builds every kext + the apps, checksums, release notes | ⚪ | `main` (`.github/workflows`) | the workflow already builds all kexts; app builds in CI, signing | [CONTRIBUTING.md](../CONTRIBUTING.md) |
| 18 | **Other GA403 variants** (GA403UI/UV/WU...) | ⚪ | - | needs reports from owners; use the Hardware report issue template | |
| 19 | **macOS 26 (Tahoe)** | 🧊 | - | not planned. Revisit after M1 | |

## How tracking works

* **This table is the source of truth.** If you start something, change its row (state, "Doing now") in the same pull request.
* Branch names: `track/<number>-<short-name>` (for example `track/04-wifi-throughput`), merged into `main` by pull request.
  Releases are tags `v0.<n>` on `main`. Retired branches are deleted after merge.
* Hardware evidence goes into [HANDOFF.md](HANDOFF.md) (dated, no secrets); decisions and design go into the topic file.
* Issues are labelled `track:<number>`, and `needs-hardware-report` for things only another owner can confirm.
* Upstream first: driver changes (NullMoth, OpenCore) are done as upstream pull requests; this repository only carries the
  patch until it is merged.

## Detailed checklists

### EFI / system

- [ ] **USB port map** for the GA403WM (USBToolBox), published and referenced from `efi/` (the internal keyboard must stay
      internal).
- [ ] Clean reference configuration: remove the no-op `IOPCIMSIMode` entries and debug boot-args; verbose off; document
      `NullMothSafe` / "start once without the NVIDIA driver" entries.
- [ ] Reference config: add `itlmt_fast=6` and the BlueToolFixup NVRAM variables to the sanitized `config.plist`.
- [ ] Tagged EFI release (config + SSDTs + checksums of the third-party kexts).
- [ ] Sleep, wake, lid close, hibernate mode (`pmset`), battery life.

### `ItlXhciKick`

- [ ] Measure the CPU/power cost of 4 x 1 kHz kicks; adapt the period (kick only controllers with HID devices, back off when ring 1
      is idle).
- [ ] Find out why interrupter 1 never raises the single vector (an enable bit would replace the polling).
- [ ] Try on other AMD generations (Phoenix, Hawk Point, Strix Halo) and report.

### `itlmt7925` (Wi-Fi)

Speed so far: link at HE / 160 MHz, 2 streams (PHY ~2.4 Gb/s); measured ~8 Mb/s down, ~4-6 Mb/s up on one run and ~5 MB/s down on
another; reorder timeouts and ~30 % firmware TX retries are the first suspects.

- [ ] **LAN baseline**: `iperf3` between the Hackintosh and a wired machine, recorded in [WIFI.md](WIFI.md) with AP model, band, distance.
- [ ] Find why RX is held up by the reorder buffer (timeouts at 100 ms; now 30 ms, `itlmt_reo=<ms>`) and why 30 % of TX frames are retried.
- [ ] TX path at the lower fast levels: track `TXFREE` instead of waiting per packet.
- [ ] A-MSDU on transmit, more than one TID, smarter ADDBA policy.
- [ ] EHT (Wi-Fi 7), 6 GHz, 80+80 MHz.
- [ ] WPA3-SAE, open networks, regulatory domain from configuration.
- [ ] Group re-key, power save, interrupts (MSI) instead of the polled worker.
- [ ] Load the firmware from a file when a filesystem is available.
- [ ] Credentials out of the kext (read from a root-only file or a user-client call) so a kext can be shared safely.
- [ ] Native Wi-Fi (`IO80211Controller`), see workstream 6.

### Apps

- [ ] RogControl: Aura effects, per-source profiles verified, localisation (VI).
- [ ] ItlWiFi: hidden networks, forget-all, better error messages; becomes optional once native Wi-Fi exists.
- [ ] Notarised / properly signed builds (today ad-hoc: every rebuild is a new identity, Gatekeeper prompts).

### Docs and project

- [ ] Screenshots (placeholders in [docs/images](images/README.md)).
- [ ] Translate the long research notes' summaries to Vietnamese.
- [ ] First release with CI-built kexts and a checksum file.

## Done recently

* ✅ internal-disk install of macOS 15.8.1, OpenCore from the internal ESP, Windows/Linux in the picker
* ✅ NullMoth 1.14.2 + local VRAM-clear and PAT patches; Zen 5 timer fix; USB lag fix
* ✅ `AsusWmi` (profiles, fans, charge limit, keyboard light, Slash, hotkeys) and `RogControl` redesign
* ✅ `AmdCppc` 0.2.1 + `AmdCppcPolicy` daemon (CPPC/EPP, sleep/wake restore)
* ✅ MT7925 Bluetooth firmware loader; BLE fix written
* ✅ ItlWiFi app (scan, join, speed test); Wi-Fi HE/160 MHz link
