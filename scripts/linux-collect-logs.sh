#!/usr/bin/env bash
# Collect everything needed to diagnose a macOS/OpenCore boot from Linux, as a normal user (after
# scripts/setup-linux-access.sh): OpenCore + ITLLOG logs from the ESP, a whitelist of NVRAM variables from
# efivars, and macOS panic reports / fsck log / unified log from the Data volume (read-only mount).
#
#   bash scripts/linux-collect-logs.sh [OUTDIR] [--unified]
#
# --unified also copies private/var/db/{diagnostics,uuidtext} (hundreds of MB) for decoding with macos-unifiedlogs.
# Output goes to ~/ga403-logs/<time> (NOT the repo: logs can hold SSIDs, serials, MACs).
set -u
UNIFIED=0; OUT=""
for a in "$@"; do case "$a" in --unified) UNIFIED=1 ;; *) OUT=$a ;; esac; done
OUT=${OUT:-$HOME/ga403-logs/$(date +%Y%m%d-%H%M%S)}
ESP=/boot; VOL=/mnt/macos-vol0; EV=/sys/firmware/efi/efivars
mkdir -p "$OUT"/{esp,nvram,macos}

echo "== ESP =="
if [ -r "$ESP/EFI/OC" ]; then
  ls -1t "$ESP"/opencore-*.txt 2>/dev/null | head -3 | while read -r f; do cp -a "$f" "$OUT/esp/"; done
  [ -f "$ESP/ITLLOG.BIN" ] && cp -a "$ESP/ITLLOG.BIN" "$OUT/esp/"
  ls -la --time-style=long-iso "$ESP/EFI/OC" "$ESP/EFI/OC/Kexts" > "$OUT/esp/tree.txt"
  sha256sum "$ESP/EFI/OC/config.plist" "$ESP/EFI/OC/OpenCore.efi" > "$OUT/esp/sha256.txt"
  ls "$OUT/esp"
else
  echo "ESP not readable: run  sudo bash scripts/setup-linux-access.sh  once"
fi

echo "== NVRAM (whitelist; SMBIOS vars MLB/ROM/SSN/HW_* are never read) =="
for n in boot-args:7c436110-ab2a-4bbb-a880-fe41995c9f82 csr-active-config:7c436110-ab2a-4bbb-a880-fe41995c9f82 \
         auto-boot:7c436110-ab2a-4bbb-a880-fe41995c9f82 opencore-version:4d1fda02-38c7-4a6a-9cc6-4bcca8b30102 \
         aapl,panic-info:7c436110-ab2a-4bbb-a880-fe41995c9f82 \
         LastBootFailed:b540a530-6978-4da7-91cb-7207d764d262; do
  f="$EV/${n%%:*}-${n#*:}"
  [ -r "$f" ] && { printf '%s = ' "${n%%:*}"; tail -c +5 "$f" | tr -d '\0' | tr -cd '[:print:]'; echo; } || echo "${n%%:*} = <not set>"
done > "$OUT/nvram/vars.txt"
cat "$OUT/nvram/vars.txt"

echo "== macOS Data volume (ro) =="
mountpoint -q "$VOL" || mount "$VOL" 2>&1 | tail -2
if mountpoint -q "$VOL" && [ -d "$VOL/Library" ]; then
  find "$VOL/Library/Logs/DiagnosticReports" "$VOL/private/var/db/PanicReporter" -type f -mtime -3 2>/dev/null \
    -exec cp -a {} "$OUT/macos/" \;
  cp -a "$VOL/private/var/log/fsck_apfs.log" "$VOL/private/var/log/system.log" "$OUT/macos/" 2>/dev/null
  [ $UNIFIED = 1 ] && { mkdir -p "$OUT/macos/logarchive"; \
    cp -a "$VOL/private/var/db/diagnostics" "$VOL/private/var/db/uuidtext" "$OUT/macos/logarchive/"; }
  ls -la --time-style=long-iso "$OUT/macos" | head -30
  umount "$VOL" && echo "unmounted $VOL"
else
  echo "macOS volume not mounted (needs setup-linux-access.sh, or the volume is dirty)"
fi
echo "== output: $OUT =="
