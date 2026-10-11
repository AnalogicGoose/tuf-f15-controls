# ga403wm-hackintosh

**OpenCore EFI, kernel extensions and notes for running macOS on the ASUS ROG Zephyrus G14 (GA403WM) with a Ryzen AI 9 HX 370
("Strix Point", Zen 5).** It includes a from-scratch Wi-Fi driver for the MediaTek MT7925 card and a fix for the USB
keyboard/mouse lag that AMD Strix Point controllers cause on macOS.

> **Experimental, one laptop.** Everything was developed on a single GA403WM and verified by booting the macOS 15.6.1
> Recovery image from a USB stick with OpenCore 1.0.9. A full installation on the internal disk has not been done yet.
> Keep a backup, boot from a USB stick first, and read [docs/DUAL-BOOT.md](docs/DUAL-BOOT.md) if you also run Windows/Linux.

## Machine

| | |
|---|---|
| Laptop | ASUS ROG Zephyrus G14 2025, model GA403WM |
| CPU / iGPU | AMD Ryzen AI 9 HX 370 (12 cores, Zen 5), Radeon 890M (disabled for macOS in `DeviceProperties`) |
| dGPU | NVIDIA GeForce RTX 5060 Laptop (Blackwell), used through [NullMoth's NVIDIA driver](https://github.com/nullmoth/nvidia-macos-driver) |
| Wi-Fi | MediaTek MT7925, PCI `14c3:7925` |
| USB | four AMD xHCI controllers `1022:151e`, `151f`, `151a`, `151b` |
| Input | I2C-HID touchpad `093a:3011`, USB keyboard `0b05:19b6` |
| Storage | NVMe SSD, Realtek SD-card reader |

## What works

| Area | State | Where |
|---|---|---|
| Boot macOS 15.6.1 Recovery with OpenCore 1.0.9 | works | [efi/](efi/README.md) |
| Zen 5 timer / FSB calculation (timers firing ~10x early) | fixed by an OpenCore patch | [patches/opencore](patches/opencore), [efi/README.md](efi/README.md#the-opencore-patch-patchesopencorefix-zen5-fsbdiff) |
| USB keyboards and mice (2 s key-release latency) | fixed by `ItlXhciKick.kext` | [docs/USB-XHCI-LAG.md](docs/USB-XHCI-LAG.md) |
| Touchpad (I2C) | works in Recovery (VoodooI2C) | [efi/KEXTS.md](efi/KEXTS.md) |
| Wi-Fi MT7925 | works: WPA2-PSK on 2.4/5 GHz, DHCP/DNS; minimal (fixed 6 Mb/s TX, no UI, no reconnect) | [docs/WIFI.md](docs/WIFI.md) |
| USB port map | **not done** (placeholder) | [docs/ROADMAP.md](docs/ROADMAP.md) |
| Full install, sleep/wake, audio, battery, display brightness, Bluetooth, webcam | **not verified** | |
| NVIDIA dGPU | third-party driver by NullMoth (macOS 15, installed inside macOS, not part of the EFI). The reference `config.plist` already carries its boot-args and SIP value; not verified by this project | [efi/README.md](efi/README.md#nvidia-dgpu-nullmoth-driver) |

## What is in this repository

```
efi/                 OpenCore config.plist + SSDTs (serial/MLB/UUID/ROM replaced by placeholders), list of third-party kexts
kexts/itlmt7925      Wi-Fi driver for the MT7925: firmware load, scan, WPA2-PSK, Ethernet interface
kexts/ItlXhciKick    keeps the second xHCI event ring serviced on single-vector AMD controllers (fixes the USB lag)
kexts/ItlDiag        boot diagnostics that log to NVRAM (clocks, xHCI, HID); a debugging aid
fw/                  MediaTek MT7925 firmware from linux-firmware, with its license
patches/opencore/    OpenCorePkg patch for Zen 5
scripts/             build, Wi-Fi credential injection, NVRAM read/clear, EFI export + privacy check, dual-boot repair
test/                host test of the WPA2 primitives (also run in CI)
tools/               helper to read Apple kexts out of a Recovery image
docs/                documentation
```

The kexts are called `itlmt7925`, `ItlXhciKick` and `ItlDiag` (bundle ids `com.itlmt7925.*`). The name comes from the Wi-Fi
driver, which started the project; the repository covers the whole machine.

## Quick start

1. **Get the kexts**: a [release](../../releases) (once published), the `kexts` artifact of the `build-kexts` workflow, or
   `bash scripts/build.sh` on a Mac.
2. **Assemble an EFI** from [efi/](efi/README.md): `config.plist`, the SSDTs, the third-party kexts listed in
   [efi/KEXTS.md](efi/KEXTS.md), and OpenCore (build the patched one with the `opencore` workflow).
   **Generate your own SMBIOS** (serial, MLB, UUID, ROM) first.
3. **Add your Wi-Fi network** to the driver (Windows, elevated shell):
   `.\scripts\inject-creds.ps1 -Kext <EFI>\OC\Kexts\itlmt7925.kext -Ssid 'MyWifi'`
4. **Boot-args**: `itlmt_m1=0 itlmt_m2=1 itlmt_m3=1` for Wi-Fi, plus the experimental `itlmt_fast=6` for HT/VHT/HE, Block Ack, A-MSDU and 160 MHz with automatic fall-back ([docs/WIFI.md](docs/WIFI.md)). `ItlXhciKick` needs none.
5. **Boot from the USB stick**, wait two or three minutes, and check the result in NVRAM
   ([docs/DEBUGGING.md](docs/DEBUGGING.md)).

## Documentation

| | |
|---|---|
| [efi/README.md](efi/README.md) | the reference EFI, boot-args, what to change for another machine |
| [efi/KEXTS.md](efi/KEXTS.md) | every kext, its upstream and role |
| [docs/WIFI.md](docs/WIFI.md) | the MT7925 driver: design, configuration, limits |
| [docs/USB-XHCI-LAG.md](docs/USB-XHCI-LAG.md) | the USB latency investigation, cause and fix |
| [docs/DUAL-BOOT.md](docs/DUAL-BOOT.md) | sharing the machine with Windows / Linux, repairing lost UEFI boot entries |
| [docs/DEBUGGING.md](docs/DEBUGGING.md) | the NVRAM log channel, variable table, helper scripts |
| [docs/ROADMAP.md](docs/ROADMAP.md) | what is missing |
| [NOTICE.md](NOTICE.md) | third-party code, firmware and license notes |

Tiếng Việt: [README.vi.md](README.vi.md).

## Privacy

* The repository never contains Wi-Fi passphrases or SMBIOS identifiers: `Info.plist` ships with empty `itl_ssid*/itl_psk*`
  keys, `efi/OC/config.plist` carries `CHANGE_ME_*` placeholders, and CI fails if real values appear. A kext you inject
  credentials into holds them in **plain text**.
* NVRAM logs contain the card's MAC address and nearby SSIDs/BSSIDs. Redact them before posting.

## License

[Clear BSD License](LICENSE) for the code in this repository, which re-implements parts of the Linux `mt76` driver
(BSD-3-Clause-Clear, © MediaTek). The firmware in `fw/` is MediaTek's and keeps [its own license](fw/LICENCE.mediatek). The
OpenCore configuration in `efi/` derives from <https://github.com/nullmoth/1401> (PolyForm Noncommercial 1.0.0) and is **not**
covered by the Clear BSD License, see [NOTICE.md](NOTICE.md).
