# itlmt7925

macOS support pieces for the **ASUS ROG Zephyrus G14 (GA403WM, Ryzen AI 9 HX 370)** and, more generally, for
AMD Strix Point laptops with a **MediaTek MT7925 Wi-Fi card**, built while bringing the machine up as a Hackintosh with
OpenCore.

| Component | What it is | State |
|---|---|---|
| [`kexts/itlmt7925`](kexts/itlmt7925) | Wi-Fi driver for the MT7925 (PCI `14c3:7925`): firmware load, scan, WPA2-PSK, Ethernet interface | works in Recovery, slow and minimal, see [docs/WIFI.md](docs/WIFI.md) |
| [`kexts/ItlXhciKick`](kexts/ItlXhciKick) | fixes ~2 s USB keyboard/mouse latency on single-vector AMD xHCI controllers | works, see [docs/USB-XHCI-LAG.md](docs/USB-XHCI-LAG.md) |
| [`kexts/ItlDiag`](kexts/ItlDiag) | boot diagnostics (clocks, xHCI, HID, NVRAM digest) | debugging aid |
| [`efi/`](efi) | the OpenCore `config.plist` and SSDTs used, serials removed | reference, see [efi/README.md](efi/README.md) |
| [`patches/opencore`](patches/opencore) | OcCpuLib fix for the Zen 5 (family 1Ah) FSB calculation | used by the reference EFI |
| [`tools/kc.py`](tools/kc.py) | helper to read Apple kexts out of a Recovery image | used for the USB analysis |

> **Experimental.** Everything here was developed and verified on one laptop, in the macOS 15.6.1 Recovery environment booted
> from a USB stick. The Wi-Fi driver has no UI, no roaming and a fixed 6 Mb/s transmit rate. Use at your own risk, keep a
> backup of your EFI, and test from a USB stick before touching an internal disk.

## Quick start

1. **Get the kexts**: a [release](../../releases) once published, or the `kexts` artifact of the `build-kexts` workflow,
   or build on a Mac: `bash scripts/build.sh`.
2. **Assemble an EFI** from [`efi/`](efi/README.md): the configuration, the SSDTs, the third-party kexts of
   [`efi/KEXTS.md`](efi/KEXTS.md), and OpenCore (the patched build comes from the `opencore` workflow).
   Generate your own SMBIOS values first.
3. **Add your network** to the kext (Windows, elevated shell):
   `.\scripts\inject-creds.ps1 -Kext <EFI>\OC\Kexts\itlmt7925.kext -Ssid 'MyWifi'`
4. **Boot-args**: `itlmt_m1=75 itlmt_m2=1 itlmt_m3=1` (Wi-Fi). `ItlXhciKick` needs none.
5. **Boot, wait two or three minutes**, then check `itls0` / `itlk*` in NVRAM ([docs/DEBUGGING.md](docs/DEBUGGING.md)).

## Documentation

| | |
|---|---|
| [docs/WIFI.md](docs/WIFI.md) | what the Wi-Fi driver does, how it is configured, limits |
| [docs/USB-XHCI-LAG.md](docs/USB-XHCI-LAG.md) | the USB latency investigation, cause and fix |
| [docs/DEBUGGING.md](docs/DEBUGGING.md) | the NVRAM log channel, variable table, helper scripts |
| [docs/ROADMAP.md](docs/ROADMAP.md) | what is missing |
| [efi/README.md](efi/README.md) | the reference EFI and what must be changed for another machine |
| [NOTICE.md](NOTICE.md) | third-party code, firmware and license notes |

Tiếng Việt: [README.vi.md](README.vi.md).

## Repository layout

```
kexts/itlmt7925      Wi-Fi driver (m1 firmware, m2 scan, m3 connect, eth interface, WPA primitives, firmware blob)
kexts/ItlXhciKick    xHCI event-ring service kext
kexts/ItlDiag        diagnostics kext
fw/                  MediaTek MT7925 firmware (linux-firmware) + its license
efi/                 reference OpenCore configuration (no binaries, no serials)
patches/opencore/    OpenCorePkg patch
scripts/             build, credential injection, NVRAM read/clear, EFI export (see each header)
test/                host test for the WPA2 primitives (also run in CI)
tools/               analysis helper
docs/                documentation
.github/workflows/   build-kexts (every push), opencore (manual)
```

## Privacy

* The repository never contains Wi-Fi passphrases: `Info.plist` ships with empty `itl_ssid*/itl_psk*` keys and CI builds with
  them empty. A kext you inject credentials into holds them in **plain text**.
* The NVRAM logs contain the card's MAC address and the SSIDs / BSSIDs of nearby networks. Redact them before posting a log.
* `scripts/export-efi.py` produces `efi/` from a working EFI and refuses to write anything that still contains the serial,
  board serial, UUID or ROM it found in the source configuration.

## License

[Clear BSD License](LICENSE) for the code in this repository, which re-implements parts of the Linux `mt76` driver
(BSD-3-Clause-Clear, © MediaTek). The firmware in `fw/` is MediaTek's and keeps [its own license](fw/LICENCE.mediatek).
See [NOTICE.md](NOTICE.md).
