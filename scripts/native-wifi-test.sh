#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# Backup / install / restore helper for the first load of itlmt7925Native (docs/TEST-NATIVE-WIFI.md). macOS only.
#   scripts/native-wifi-test.sh backup            copy ESP:/EFI to ~/itlmt-backup/EFI-<time> (+ tar.gz) and print the path
#   scripts/native-wifi-test.sh install <kext>    backup first, then copy the kext to OC/Kexts and add the Kernel/Add entry + boot-args
#   scripts/native-wifi-test.sh restore [dir]     put the newest (or given) backup back, replacing ESP:/EFI
#   scripts/native-wifi-test.sh logs              collect kextstat/ioreg/log of the last boot into ~/itlmt-backup/logs-<time>
# ESP is taken from /Volumes/ESP (scripts/esp-automount.sh) or from ESP=/path.
set -eu
ESP=${ESP:-/Volumes/ESP}
BK=$HOME/itlmt-backup
TS=$(date +%Y%m%d-%H%M%S)
[ -d "$ESP/EFI/OC" ] || { echo "no $ESP/EFI/OC: mount the ESP first (scripts/esp-automount.sh or diskutil mount)"; exit 1; }
mkdir -p "$BK"

backup() {
  d=$BK/EFI-$TS
  cp -Rp "$ESP/EFI" "$d"
  tar -czf "$d.tar.gz" -C "$BK" "EFI-$TS"
  # NVRAM boot-args as well: it overrides config.plist when set
  nvram boot-args > "$d.nvram-boot-args.txt" 2>&1 || true
  echo "$d"
}

case "${1:-}" in
backup) backup;;
install)
  k=${2:?path to itlmt7925Native.kext}
  [ -f "$k/Contents/Info.plist" ] || { echo "not a kext: $k"; exit 1; }
  d=$(backup); echo "backup: $d"
  rm -rf "$ESP/EFI/OC/Kexts/itlmt7925Native.kext"
  cp -Rp "$k" "$ESP/EFI/OC/Kexts/"
  /usr/bin/python3 -I - "$ESP/EFI/OC/config.plist" <<'PY'
import plistlib, sys
p = sys.argv[1]
c = plistlib.load(open(p, 'rb'))
add = c['Kernel']['Add']
names = [a['BundlePath'] for a in add]
if 'itlmt7925Native.kext' not in names:
    core = next(i for i, a in enumerate(add) if a['BundlePath'] == 'itlmt7925.kext')
    e = {'Arch': 'Any', 'BundlePath': 'itlmt7925Native.kext', 'Comment': 'native Wi-Fi test', 'Enabled': True,
         'ExecutablePath': 'Contents/MacOS/itlmt7925Native', 'MaxKernel': '', 'MinKernel': '', 'PlistPath': 'Contents/Info.plist'}
    add.insert(core + 1, e)       # must come after the core kext it depends on
a = c['NVRAM']['Add']['7C436110-AB2A-4BBB-A880-FE41995C9F82']
ba = a.get('boot-args', '')
for t in ('itlmt_native=1', 'itlmt_native_if=1'):
    if t not in ba.split():
        ba += ' ' + t
a['boot-args'] = ba.strip()
plistlib.dump(c, open(p, 'wb'))
print('config.plist: Add entry after itlmt7925.kext, boot-args =', a['boot-args'])
PY
  echo "done. Reboot. To go back: scripts/native-wifi-test.sh restore";;
restore)
  d=${2:-$(ls -d "$BK"/EFI-* 2>/dev/null | grep -v '\.' | tail -1)}
  [ -d "$d" ] || { echo "no backup directory"; exit 1; }
  rm -rf "$ESP/EFI"; cp -Rp "$d" "$ESP/EFI"
  echo "restored $d -> $ESP/EFI; reboot. Check: nvram boot-args (reset NVRAM from the OC picker if the native args are stuck)";;
logs)
  o=$BK/logs-$TS; mkdir -p "$o"
  kextstat 2>/dev/null | grep -i itlmt > "$o/kextstat.txt" || true
  ioreg -l -w0 | grep -i -B2 -A12 'itlmt7925' > "$o/ioreg.txt" || true
  log show --last boot --predicate 'eventMessage CONTAINS[c] "itlmt" OR process == "kernel"' --style compact > "$o/log.txt" 2>&1 || true
  dmesg | grep -i itlmt > "$o/dmesg.txt" || true
  nvram boot-args > "$o/boot-args.txt" || true
  ifconfig -a | grep -E '^[a-z]+[0-9]+:' > "$o/interfaces.txt"
  echo "$o (check for SSIDs/MACs before sharing)";;
*) echo "usage: $0 backup | install <kext> | restore [dir] | logs"; exit 2;;
esac
