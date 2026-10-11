#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# Linux half of the 2026-10-11 changes. RUN ON LINUX (CachyOS) from the repo, no sudo (needs setup-linux-access.sh once).
#   1. itlmt7925.kext (runtime control, start when the system is ready) -> /boot/EFI/OC/Kexts   (credentials carried over)
#   2. ItlDiag.kext (opens the ESP only per flush, so macOS can mount it again)  -> /boot/EFI/OC/Kexts
#   3. boot-args: itlmt_m1=75 -> itlmt_m1=0 (no fixed 75 s wait any more)        (occonfig, backup first)
#   4. occonfig check (ocvalidate when it is built)
# The kexts are the ones built on macOS (build/ in the repo on the macOS Data volume, read-only mount /mnt/macos-vol0).
# Undo: bash scripts/linux-install-kext.sh --undo itlmt7925 ; ... --undo ItlDiag ; the config backup is
#       /boot/EFI/OC/config.plist.m1-<time> (cp it back over config.plist).
set -euo pipefail
cd "$(dirname "$0")/.."
CFG=/boot/EFI/OC/config.plist
[ -w "$CFG" ] || { echo "$CFG not writable: run sudo bash scripts/setup-linux-access.sh once"; exit 1; }
mountpoint -q /mnt/macos-vol0 || mount /mnt/macos-vol0

echo "== 1/4 itlmt7925 =="
bash scripts/linux-install-kext.sh itlmt7925
echo "== 2/4 ItlDiag =="
bash scripts/linux-install-kext.sh ItlDiag
echo "== 3/4 boot-args =="
python3 tools/occonfig.py -c "$CFG" args add itlmt_m1=0 --tag "m1-$(date +%Y%m%d-%H%M%S)"
echo "== 4/4 check =="
python3 tools/occonfig.py -c "$CFG" check || echo "occonfig check reported problems: read them before rebooting"
python3 tools/occonfig.py -c "$CFG" args show | tr '\n' ' '; echo
sync
echo
echo "Done. Reboot into macOS (OpenCore picker). Then on macOS:"
echo "  * Wi-Fi icon (ItlWiFi) in the menu bar; the driver starts a few seconds after boot, no 75 s wait"
echo "  * bash scripts/macos-vramtest.sh          (NVRMFB patch: expect tens of ms, was 0.7-1.1 s)"
echo "  * bash scripts/macos-install-hidpi.sh     (if not done yet; reboot, then Displays > 1440 x 900)"
echo "  * sudo diskutil mount disk0s1             (should work now that ItlDiag no longer holds the ESP)"
