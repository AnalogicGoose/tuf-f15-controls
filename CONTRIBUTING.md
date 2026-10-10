# Contributing

Reports and patches are welcome, especially: results on other Zephyrus G14 / Strix Point laptops (`ItlXhciKick`, the EFI),
other MT7925 machines (the Wi-Fi driver), and the open items in [docs/ROADMAP.md](docs/ROADMAP.md).

## Branches: where development happens

| Branch | State |
|---|---|
| **`main`** | **The only active branch.** Everything is developed here (directly or through a short-lived feature branch + pull request). Every push builds the kexts in CI. |
| `ccr-fc111298-ip8jp5` | Fast-mode Wi-Fi work (HT/VHT, Block Ack, A-MSDU, 160 MHz, HE). Merged into `main` by PR #2 (`4bd83f1`). Retired, kept for history only. |
| `diag-esp-log` | ItlDiag writes `ITLLOG.BIN` to the ESP. Merged by PR #2. Retired. |

Start new work from `main`: `git switch main && git pull && git switch -c <topic>`.

* **macOS:** `bash scripts/build.sh` builds `build/*.kext` locally (needs Xcode command line tools).
* **Linux / Windows:** the kexts cannot be built there (they need the macOS SDK). Push the branch and let CI build it:
  `gh workflow run build.yml --ref <branch>`, then `gh run download <run-id> -n kexts` (artifacts carry empty Wi-Fi
  credentials; inject yours locally with `scripts/inject-creds.ps1`).
* The per-machine state (which kext is on the ESP, current boot-args, bisect notes) lives outside the repo; the
  `efi/` tree is the sanitized reference configuration.

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
* Keep the files under the Clear BSD License (`SPDX-License-Identifier: BSD-3-Clause-Clear` on top); `efi/` is the exception, see `NOTICE.md`.
