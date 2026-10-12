# Roadmap and work tracking

Last updated: 2026-10-12 (HiDPI corrected; codebase standardisation and release v1 added). This page is the single place that says **what is being worked on, where, and what is next**.
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
| **M1** | **Stable macOS 15 for GA403WM** (public beta) | a second owner reproduces the install from this repo; touchpad works; published USB map; clean (non-debug) EFI; OLED brightness verified; sleep/wake verified; Wi-Fi at a usable speed (>= 100 Mb/s). Release **v1** (prebuilt kexts + apps) comes first, as the starting point for those testers | 🔧 |
| **M1.5** | **Standardise the codebase** (cleanup, common call conventions) | only per component, only after it is proven stable (see [Codebase standardisation](#codebase-standardisation)); never before M1 for the Wi-Fi driver | ⚪ gated |
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
| 13 | **HiDPI** | 🟡 | `main` (`scripts/macos-install-hidpi.sh`, `tools/hidpi-try.swift`) | "looks like 1440 x 900" works on the reference unit with the single same-size HiRes override (user report 2026-10-11); other scales (1680x1050, 1280x800) still go black because NVRMFB cannot do scaled mode sets. Next: second-owner confirmation, upstream flag fix so no override is needed | [SCRIPTS.md §4](SCRIPTS.md#4-hidpi-looks-like-1440-x-900-step-by-step), [HANDOFF #7](HANDOFF.md) |
| 14 | **Black screen / flicker with 120 Hz + modeset** | 🟡 | NullMoth upstream | stable on 1.13-1.14 with the boot raster at 120 Hz; do not change modes at login | [HANDOFF #2](HANDOFF.md), [NULLMOTH.md](NULLMOTH.md) |
| 15 | **Boot time** (~36 s kernel to login) | 🟡 | `main` (EFI) | OpenCore debug target off, `-nvrmnobootscreen`, verbose off; remaining: GOP console cost | [HANDOFF #9](HANDOFF.md) |
| 16 | **Unverified hardware**: audio, SD reader, battery %, webcam, sleep/wake, lid, Fn keys | ⚪ | - | go through them one by one; each result into [HANDOFF.md](HANDOFF.md) and the README table | [README](../README.md#what-works) |
| 17 | **Release v1**: prebuilt kexts, apps, CPU daemon, patched OpenCore, reference EFI, checksums | 🔧 | `main` (`.github/workflows/build.yml`, `.github/release-body.md`) | workflow ready (tag `v1` publishes); app/OpenCore build steps never run in CI yet: run `build-kexts` by hand (workflow_dispatch) and fix, then tag. Later: Developer ID signing + notarisation | [SCRIPTS.md §1](SCRIPTS.md#1-use-the-prebuilt-release-v1) |
| 18 | **Other GA403 variants** (GA403UI/UV/WU...) | ⚪ | - | needs reports from owners; use the Hardware report issue template | |
| 19 | **Codebase standardisation** (call conventions, shared helpers, cleanup of debug/dead code, scripts) | ⚪ gated | `track/19-*` per component | starts per component only after its stability gate | [below](#codebase-standardisation) |
| 20 | **Scripts guide** (what to run, undo, author-specific flags) | ✅ | `main` | [SCRIPTS.md](SCRIPTS.md); next: remove author-specific paths (part of 19) | [SCRIPTS.md](SCRIPTS.md) |
| 21 | **macOS 26 (Tahoe)** | 🧊 | - | not planned. Revisit after M1 | |

## Codebase standardisation

**Rule: do not clean up what is not proven.** The code was written fast, by an AI, while the hardware was still being figured out, so it
carries debug paths, duplicated helpers and one-off scripts. Tidying it now would mean refactoring things nobody has shown to be
right, and every refactor risks breaking a hard-won behaviour (a kernel panic is the price). So each component gets cleaned up
**after** it passes its stability gate, and the cleanup must not change behaviour.

### Stability gate (per component)

A component is "proven stable" when all of these hold; record the evidence in the workstream row and in [HANDOFF.md](HANDOFF.md):

* in daily use on the reference unit for **at least 14 days** across reboots, sleep/wake and AC/battery changes, with **no panic or hang**
  traced to it;
* confirmed on **at least one other** GA403 (or the component is hardware-independent), via a Hardware report;
* its known issues are closed or written down as accepted limits;
* a recorded **baseline** exists to compare against after the cleanup (a log of `sysctl`/`ioreg` output, a benchmark, or a
  host test), and the component has at least a smoke test.

### What gets standardised, in this order

| Order | Component | Standardisation work | Gate |
|---|---|---|---|
| 1 | **Scripts** (no kernel risk, so it can start early) | remove author-specific paths (`~/efi-audit`, `/Users/...`, `/dev/nvme0n1p4`, `~/develop/...`); one `scripts/lib.sh` (ESP discovery, backup, privilege wrapper, `--undo`/`--dry-run`/`--help` convention); delete obsolete ones (NullMoth 1.13 byte patch, rejected PR #139 candidate, the `*-2026-10-11*` batches) or move them to `scripts/archive/`; `shellcheck` in CI; group by purpose as in [SCRIPTS.md](SCRIPTS.md) | none (start any time) |
| 2 | **Docs** | fold [HANDOFF.md](HANDOFF.md) findings into the topic files and keep HANDOFF as a short changelog; remove stale statements (e.g. HiDPI "not possible"); one status source | none |
| 3 | `ItlXhciKick`, `AmdCppc` + `AmdCppcPolicy` | smallest and most stable: consistent logging into the registry, sysctl registration helper, error codes (`kIOReturn*` everywhere), remove diagnostic-only sysctls that are no longer used, one set of boot-arg names | gate passed |
| 4 | `AsusWmi` and `RogControl` | **one ABI header** shared by kext and app (versioned, documented), one way to evaluate ACPI/WMI methods, one place for retry/validation rules; remove `probe`/`dumpacpi`-style debug entry points or hide them behind a boot-arg; split `RogControl.swift` (1100 lines) into driver access, state, views; share the IOKit user-client wrapper with ItlWiFi | gate passed; fan-curve and charge-limit writes verified |
| 5 | `MtkBtFw`, `BtLeDlFix` | same helpers; treat "no answer to the first PATCH_DWNLD query" correctly; pattern table for `BtLeDlFix` per macOS build | BLE and pairing verified |
| 6 | **EFI / `config.plist`** | no debug boot-args, no `IOPCIMSIMode`, no `ItlDiag`, explicit comments per patch, published USB map | M1 |
| 7 | **`itlmt7925`** (last: biggest and the least finished) | rename `_m1/_m2/_m3/_pwr/_eth/_ba/_uc` to what they do; separate hardware layer from 802.11 logic; one TX/RX path instead of the legacy + fast levels; remove `itlmt_fast` levels that proved useless; credentials out of the `Info.plist`; keep host tests green | throughput and reconnect proven; after (or together with) native Wi-Fi |
| 8 | **Bundle ids and names** (`com.itlmt7925.*`, `Itl*`) | rename to the project's name only at a major version, with a migration note, because `config.plist`, LaunchAgents and the auxiliary kernel collection refer to them | v2 |

### How a standardisation change is done

1. branch `track/19-<component>`, one component per pull request, **no behaviour change in the same commit as a rename**;
2. take the baseline *before*, run the same measurement *after*, attach both to the pull request;
3. keep the old interface for one release (the ABI headers carry a version), remove it in the next;
4. the release notes of the next tag say what was renamed, so people can update their `config.plist` and scripts;
5. if a regression shows up on hardware, revert the whole pull request; do not patch forward on a kernel component.

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
- [ ] Notarised / properly signed builds (today ad-hoc: users must strip the quarantine attribute, every rebuild is a new identity).

### Docs and project

- [ ] Screenshots (placeholders in [docs/images](images/README.md)).
- [ ] Translate the long research notes' summaries to Vietnamese.
- [ ] **Release v1**: run `build-kexts` by hand, fix the app / OpenCore build steps if they fail, tag `v1`; check that the release has kexts, apps, `AmdCppcPolicy`, OpenCore, `EFI-reference.zip` and `SHA256SUMS.txt`.
- [ ] Release v1.x: Developer ID signing and notarisation of the apps; signed installer package for the kexts and daemon.

## Done recently

* ✅ internal-disk install of macOS 15.8.1, OpenCore from the internal ESP, Windows/Linux in the picker
* ✅ NullMoth 1.14.2 + local VRAM-clear and PAT patches; Zen 5 timer fix; USB lag fix
* ✅ `AsusWmi` (profiles, fans, charge limit, keyboard light, Slash, hotkeys) and `RogControl` redesign
* ✅ `AmdCppc` 0.2.1 + `AmdCppcPolicy` daemon (CPPC/EPP, sleep/wake restore)
* ✅ MT7925 Bluetooth firmware loader; BLE fix written
* ✅ ItlWiFi app (scan, join, speed test); Wi-Fi HE/160 MHz link
* ✅ HiDPI "looks like 1440 x 900" at 120 Hz via the same-size HiRes override ([SCRIPTS.md §4](SCRIPTS.md#4-hidpi-looks-like-1440-x-900-step-by-step))
