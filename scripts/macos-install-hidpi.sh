#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# HiDPI ("200 %") for the built-in OLED on macOS: adds a display override with scale-resolutions only. RUN ON macOS (sudo).
#   scripts/macos-install-hidpi.sh          install; reboot, then switch with tools/hidpi-try.swift
#   scripts/macos-install-hidpi.sh --undo   remove it again
# Unlike the Linux installer (install-display-override.sh) this does NOT replace the EDID and does NOT delete the
# WindowServer prefs: the panel keeps the timing it boots with (2880x1800 @ 120 Hz), so picking a HiDPI size is a scaling
# change inside WindowServer, not a new mode for NVKMS. Entries (backing size, macOS shows half of it):
#   2880x1800 -> looks like 1440x900 (exact 2x, like Windows 200 %)
#   3360x2100 -> looks like 1680x1050 (more space; rendered larger and scaled down, costs more GPU)
#   2560x1600 -> looks like 1280x800  (larger text)
set -euo pipefail
VID=4c83; PID=419c
DIR=/Library/Displays/Contents/Resources/Overrides/DisplayVendorID-$VID
F=$DIR/DisplayProductID-$PID
if [ "${1:-}" = "--undo" ]; then
  sudo rm -f "$F"; echo "removed $F; reboot (or log out) to drop the HiDPI sizes"; exit 0
fi
cur=$(ioreg -l -w0 2>/dev/null | grep -E '"DisplayVendorID" = |"DisplayProductID" = ' | sort -u | tr -d ' "|' | tr '\n' ' ')
case "$cur" in *DisplayProductID=16796*DisplayVendorID=19587*) ;; *) echo "built-in panel is not 4c83:419c ($cur): not installing"; exit 1;; esac
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
/usr/bin/python3 -I - "$T/o.plist" <<'PY'
import plistlib, struct, sys
def ent(w, h): return struct.pack(">IIII", w, h, 1, 0x00200000)   # flag 1 + 0x200000: HiDPI entry
plistlib.dump({"DisplayVendorID": 0x4c83, "DisplayProductID": 0x419c, "DisplayProductName": "Built-in OLED",
               "scale-resolutions": [ent(2880, 1800), ent(3360, 2100), ent(2560, 1600)]}, open(sys.argv[1], "wb"))
PY
plutil -lint "$T/o.plist" >/dev/null
sudo mkdir -p "$DIR"
[ -f "$F" ] && sudo cp -p "$F" "$F.bak-$(date +%Y%m%d-%H%M%S)"
sudo cp "$T/o.plist" "$F"; sudo chown root:wheel "$F"; sudo chmod 644 "$F"
echo "installed $F. Reboot, then switch ONLY with tools/hidpi-try.swift (not System Settings: its 1440 x 900 can be the low-resolution mode that goes black)."
