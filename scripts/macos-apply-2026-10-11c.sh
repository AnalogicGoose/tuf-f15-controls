#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# Third round of 2026-10-11, everything that needs sudo. RUN ON macOS as your user (password once), then reboot.
#   1. kernel message buffer of this boot (dmesg) -> ~/efi-audit, to find the ~17 s stall right after the kernel starts
#   2. itlmt7925 -> ESP: the fall-back ladder works again (it lost the credentials after the first level, so a level that
#      did not pass unicast ended in "nocred" forever = Wi-Fi never came back after boot); the working level is remembered
#   3. HiDPI: WindowServer pref DisplayResolutionEnabled + the scale-resolutions override. After the reboot, switch with
#      tools/hidpi-try.swift, which only takes a true HiDPI mode (native 2880x1800 framebuffer) and reverts by itself in 15 s
set -u
cd "$(dirname "$0")/.."
OUT="$HOME/efi-audit/apply-c-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$OUT"
sudo -v || exit 1

echo "== 1/3 kernel messages of this boot =="
sudo dmesg > "$OUT/dmesg.txt" 2>&1; sudo chown "$(id -u)" "$OUT/dmesg.txt"
echo "$(wc -l < "$OUT/dmesg.txt") lines -> $OUT/dmesg.txt"

echo "== 2/3 itlmt7925 -> ESP =="
bash scripts/build.sh itlmt7925 > "$OUT/build.txt" 2>&1 || { echo "build failed, see $OUT/build.txt"; exit 1; }
bash scripts/macos-install-kext.sh itlmt7925 2>&1 | tee "$OUT/kext.txt"

echo "== 3/3 HiDPI =="
sudo defaults write /Library/Preferences/com.apple.windowserver.plist DisplayResolutionEnabled -bool true
bash scripts/macos-install-hidpi.sh 2>&1 | tail -1

echo
echo "Done ($OUT). Reboot (sudo shutdown -r now). After the reboot:"
echo "  swiftc -O tools/hidpi-try.swift -o /tmp/hidpi-try && /tmp/hidpi-try list     # HiDPI modes are marked"
echo "  /tmp/hidpi-try 1440           # try 'looks like 1440 x 900'; Return within 15 s keeps it, otherwise it reverts"
echo "  /tmp/hidpi-try 1440 --keep    # keep it permanently"
echo "Undo HiDPI: bash scripts/macos-install-hidpi.sh --undo; sudo defaults delete /Library/Preferences/com.apple.windowserver.plist DisplayResolutionEnabled"
