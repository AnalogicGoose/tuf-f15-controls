#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# Second round of 2026-10-11, everything that needs sudo. RUN ON macOS as your user (password once), then reboot.
#   1. ESP: mount it from macOS (tests the ItlDiag fix), list the kexts, copy the newest OpenCore logs to ~/efi-audit
#   2. NVRMFB patch: VRAM grant timing after the patch (was 0.7-1.1 s per 8 MB)
#   3. itlmt7925: reorder timeout 30 ms (was 100 ms, 23 % of the time stalled), signal decoder + rssi-probe   -> ESP
#   4. boot-arg -nvrmnobootscreen: NullMoth's boot-screen copy read 28 MB of VRAM uncached, 15-19 s per boot -> ESP config
#   5. remove the HiDPI override: NullMoth offers no HiDPI modes and every scaled mode (1440x900 etc.) goes black
set -u
cd "$(dirname "$0")/.."
OUT="$HOME/efi-audit/apply-b-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$OUT"
sudo -v || exit 1
( while true; do sudo -n true; sleep 50; kill -0 "$$" 2>/dev/null || exit; done ) 2>/dev/null &
KEEP=$!
ESP=disk0s1
MP=""
cleanup() { [ -n "$MP" ] && sudo diskutil unmount "$ESP" >/dev/null 2>&1; kill $KEEP 2>/dev/null; }
trap cleanup EXIT

echo "== 1/5 ESP mount (ItlDiag fix) =="
if sudo diskutil mount "$ESP" 2>&1 | tee "$OUT/esp-mount.txt"; then
  MP=$(diskutil info "$ESP" | awk -F': *' '/Mount Point/{print $2}')
fi
if [ -n "$MP" ]; then
  echo "ESP mounted at $MP: ItlDiag no longer blocks it"
  ls -la "$MP/EFI/OC/Kexts" > "$OUT/esp-kexts.txt" 2>&1
  ls -t "$MP"/EFI/OC/opencore-*.txt "$MP"/opencore-*.txt 2>/dev/null | head -3 | while read -r f; do cp "$f" "$OUT/"; done
  ls "$OUT"/opencore-*.txt >/dev/null 2>&1 && echo "OpenCore logs copied to $OUT" || echo "no OpenCore log files on the ESP (file logging off)"
else
  echo "ESP still does not mount: steps 3 and 4 skipped (log in $OUT)"
fi

echo "== 2/5 VRAM grant timing (after the NVRMFB patch) =="
bash scripts/macos-vramtest.sh 2>&1 | tee "$OUT/vramtest-after.txt"

if [ -n "$MP" ]; then
  echo "== 3/5 itlmt7925 -> ESP =="
  bash scripts/build.sh itlmt7925 > "$OUT/build.txt" 2>&1 || { echo "build failed, see $OUT/build.txt"; exit 1; }
  bash scripts/macos-install-kext.sh itlmt7925 2>&1 | tee "$OUT/kext.txt"
  echo "== 4/5 boot-arg -nvrmnobootscreen =="
  sudo /usr/bin/python3 tools/occonfig.py -c "$MP/EFI/OC/config.plist" args --tag "nobootscreen-$(date +%Y%m%d-%H%M%S)" add -- -nvrmnobootscreen 2>&1 |
    tee "$OUT/bootargs.txt"
  /usr/bin/python3 tools/occonfig.py -c "$MP/EFI/OC/config.plist" args show | tr '\n' ' ' | tee -a "$OUT/bootargs.txt"; echo
fi

echo "== 5/5 remove the HiDPI override =="
bash scripts/macos-install-hidpi.sh --undo

echo
echo "Done ($OUT). Reboot now (sudo shutdown -r now). Afterwards: ItlWiFi > Speed Test, and keep the resolution at 2880 x 1800."
echo "Undo: kext  bash scripts/macos-install-kext.sh --undo itlmt7925"
echo "      boot-arg  the config backup config.plist.nobootscreen-<time> sits next to EFI/OC/config.plist"
