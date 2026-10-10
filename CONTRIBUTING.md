# Contributing

Reports and patches are welcome, especially: results on other machines (xHCI `ItlXhciKick`), other MT7925 laptops, and
the open items in [docs/ROADMAP.md](docs/ROADMAP.md).

## Before you post a log, a config or a kext

* **Redact** serial numbers, MLB, SystemUUID, ROM, Wi-Fi passphrases, your MAC address and the SSIDs/BSSIDs of other
  networks. NVRAM logs contain the last two.
* Never commit a `config.plist` with your SMBIOS values. `scripts/export-efi.py` writes `efi/` from a working EFI with
  placeholders, and `scripts/check-efi-privacy.py` (also run in CI) fails if real values slipped in.
* Never commit a kext whose `Info.plist` has `itl_psk*` filled in.

## Building and testing

```bash
cd test && clang++ -std=c++17 -I../kexts/itlmt7925 wpa_test.cpp ../kexts/itlmt7925/itl_wpa.cpp -o wpa_test && ./wpa_test
bash scripts/build.sh            # macOS with Xcode command line tools
```

The driver can only be exercised on hardware. Please say in the pull request which machine, macOS build and boot
environment (Recovery or installed) you tested on, and attach the redacted `itlk*` / `itls0` / `itlz0` output.

## Code

* Hardware behaviour is ported from the Linux `mt76` driver; cite the Linux function in a comment next to the port and keep
  the MediaTek copyright notice.
* No patching of Apple binaries, no raw writes to registers the Linux driver does not touch, without a written reason.
* Keep the files under the Clear BSD License (`SPDX-License-Identifier: BSD-3-Clause-Clear` on top).
