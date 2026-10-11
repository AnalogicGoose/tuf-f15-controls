#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# Round d of 2026-10-11 (sudo). RUN ON macOS as your user, then reboot.
#   AmdCppc.kext -> ESP + a Kernel/Add entry in config.plist (backup first). It only reads CPU power registers at boot;
#   changes happen only through `sudo sysctl debug.amdcppc.*` afterwards (see kexts/AmdCppc/AmdCppc.cpp).
set -euo pipefail
cd "$(dirname "$0")/.."
sudo -v
bash scripts/build.sh AmdCppc >/dev/null
bash scripts/macos-install-kext.sh AmdCppc 2>&1 | grep -v "WARNING: AmdCppc.kext is not listed"
ESP=disk0s1
MP=$(diskutil info "$ESP" | awk -F': *' '/Mount Point/{print $2}')
M=0; if [ -z "$MP" ]; then sudo diskutil mount "$ESP" >/dev/null; M=1; MP=$(diskutil info "$ESP" | awk -F': *' '/Mount Point/{print $2}'); fi
trap '[ $M = 1 ] && sudo diskutil unmount "$ESP" >/dev/null 2>&1 || true' EXIT
C="$MP/EFI/OC/config.plist"
if /usr/bin/python3 tools/occonfig.py -c "$C" get "Kernel/Add/[BundlePath=AmdCppc.kext]" >/dev/null 2>&1; then
  echo "config: AmdCppc already listed"
else
  sudo /usr/bin/python3 tools/occonfig.py -c "$C" add --tag "amdcppc-$(date +%Y%m%d-%H%M%S)" Kernel/Add \
    '{"Arch":"x86_64","BundlePath":"AmdCppc.kext","Comment":"AMD CPU power registers: read at boot, change via sysctl debug.amdcppc","Enabled":true,"ExecutablePath":"Contents/MacOS/AmdCppc","MaxKernel":"","MinKernel":"","PlistPath":"Contents/Info.plist"}' | tail -1
fi
/usr/bin/python3 tools/occonfig.py -c "$C" check 2>&1 | head -1
echo "Done. Reboot, then: sysctl debug.amdcppc.report"
