#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# Everything from the 2026-10-11 session that needs sudo, in one run. RUN ON macOS as your user; it asks for the password once.
# Each step stops the run if it fails; steps already done are listed with their undo command at the end.
#   1. measure the NullMoth VRAM grant time (before)          scripts/macos-vramtest.sh
#   2. NVRMFB: 8-byte VRAM clear (PR #64 idea), rebuild auxKC  scripts/macos-nullmoth-vramclear.sh
#   3. itlmt7925 with runtime control -> ESP                   scripts/build.sh + scripts/macos-install-kext.sh
#   4. HiDPI sizes for the built-in panel (no EDID change)     scripts/macos-install-hidpi.sh
#   5. ItlWiFi menu bar app -> /Applications (no sudo)         scripts/build-itlwifi.sh --install
# Then reboot once.
set -u
cd "$(dirname "$0")/.."
OUT="$HOME/efi-audit/apply-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$OUT"
DONE=()
fail() { echo; echo "STOPPED at: $1 (log in $OUT)"; summary; exit 1; }
summary() {
  echo; echo "== done =="
  for d in "${DONE[@]+"${DONE[@]}"}"; do echo "  $d"; done
}
sudo -v || exit 1
( while true; do sudo -n true; sleep 50; kill -0 "$$" 2>/dev/null || exit; done ) 2>/dev/null &   # keep sudo alive
KEEP=$!; trap 'kill $KEEP 2>/dev/null' EXIT

# Safe to run again: finished steps are skipped or repeated harmlessly.
if bash scripts/macos-nullmoth-vramclear.sh --check 2>/dev/null | grep -q "already patched"; then
  echo "== 1/5 NVRMFB already patched: timing after the patch =="
  bash scripts/macos-vramtest.sh 2>&1 | tee "$OUT/vramtest.txt" || true
else
  echo "== 1/5 VRAM grant timing (before) =="
  bash scripts/macos-vramtest.sh 2>&1 | tee "$OUT/vramtest-before.txt" || true
fi

echo "== 2/5 NVRMFB 8-byte clear =="
bash scripts/macos-nullmoth-vramclear.sh 2>&1 | tee "$OUT/vramclear.txt"
[ "${PIPESTATUS[0]}" = 0 ] || fail "NVRMFB patch (nothing changed by it)"
DONE+=("NVRMFB patched            undo: bash scripts/macos-nullmoth-vramclear.sh --undo")

echo "== 3/5 itlmt7925 -> ESP =="
bash scripts/build.sh itlmt7925 > "$OUT/build.txt" 2>&1 || fail "itlmt7925 build"
bash scripts/build.sh ItlDiag > "$OUT/build-itldiag.txt" 2>&1 || fail "ItlDiag build"
if bash scripts/macos-install-kext.sh itlmt7925 2>&1 | tee "$OUT/kext.txt" && [ "${PIPESTATUS[0]}" = 0 ] &&
   bash scripts/macos-install-kext.sh ItlDiag 2>&1 | tee -a "$OUT/kext.txt" && [ "${PIPESTATUS[0]}" = 0 ]; then
  DONE+=("itlmt7925 + ItlDiag on the ESP   undo: bash scripts/macos-install-kext.sh --undo itlmt7925 (and ItlDiag)")
  DONE+=("NOTE: boot-arg itlmt_m1=75 -> 0 still to do (Linux: scripts/linux-apply-2026-10-11.sh does it)")
else
  DONE+=("SKIPPED kexts: ESP busy (old ItlDiag holds it). Do it from Linux: bash scripts/linux-apply-2026-10-11.sh")
fi

echo "== 4/5 HiDPI override =="
bash scripts/macos-install-hidpi.sh 2>&1 | tee "$OUT/hidpi.txt"
[ "${PIPESTATUS[0]}" = 0 ] || fail "HiDPI override"
DONE+=("HiDPI override            undo: bash scripts/macos-install-hidpi.sh --undo")

echo "== 5/5 ItlWiFi app =="
bash scripts/build-itlwifi.sh --install 2>&1 | tee "$OUT/itlwifi.txt"
[ "${PIPESTATUS[0]}" = 0 ] || fail "ItlWiFi app"
DONE+=("ItlWiFi in /Applications  undo: rm -rf /Applications/ItlWiFi.app")

summary
cat <<EOF

Now reboot (sudo shutdown -r now). After the reboot:
  * Displays: System Settings > Displays > pick "1440 x 900", refresh stays 120 Hz
  * Wi-Fi: wait for the itlmt_m1 delay (75 s), then use the Wi-Fi icon of ItlWiFi in the menu bar
  * VRAM: bash scripts/macos-vramtest.sh   (compare with $OUT/vramtest-before.txt)
If the screen stays black after the reboot, undo without the GUI from macOS Recovery (Terminal; "Data" is your Data volume):
  D="/Volumes/MacOS - Data"           # in Recovery: ls /Volumes (mount it in Disk Utility first)
  rm -rf "\$D/Library/Extensions/NVRMFB.kext"
  cp -R "\$D/Library/NullMoth/vramclear/NVRMFB.kext" "\$D/Library/Extensions/"
  cp "\$D/Library/NullMoth/vramclear/AuxiliaryKernelExtensions.kc" "\$D/Library/KernelCollections/"
  rm -f "\$D/Library/Displays/Contents/Resources/Overrides/DisplayVendorID-4c83/DisplayProductID-419c"
Do NOT use the OpenCore entry "1401: Remove NVIDIA driver" for this: it also restores the pre-NullMoth config.plist
(UTBMap, the ALIB patch and the itlmt boot-args would be lost).
EOF
