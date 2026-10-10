#!/usr/bin/env bash
# Tidy the ESP (/boot) after bring-up sessions. Dry run by default; --apply MOVES the files (never deletes) to
# ~/ga403-esp-trash/<time>/ with the same relative paths, so everything can be put back with cp -a.
#
#   bash scripts/esp-cleanup.sh            list what would move
#   bash scripts/esp-cleanup.sh --apply    move it
#
# Moved:
#   EFI/OC/FSCK0000.000                   fsck leftover of a config.plist backup
#   EFI/OC/Kexts/*.nullmoth-*             kext dirs the NullMoth installer parked; OpenCore never loads them
#   EFI/OC/config.plist.*                 all backups except the newest N (KEEP_CFG, default 2; mtime, see below) and the one named in
#                                         /Library/NullMoth/state (config.plist.nullmoth-20261010-040128): 1401 --remove restores it
#   opencore-*.txt                        all but the newest N (KEEP_LOGS, default 4)
#   itlmt7925.*.old                       old kext backup in the ESP root
# Left alone: SysReport/ (OpenCore only dumps ACPI when it is absent), Linux boot files, .Trashes/.fseventsd/.Spotlight-V100.
# Needs the ESP writable by you (scripts/setup-linux-access.sh), no sudo.
set -u
ESP=${ESP:-/boot}; OC=$ESP/EFI/OC
KEEP_CFG=${KEEP_CFG:-2}; KEEP_LOGS=${KEEP_LOGS:-4}
APPLY=0; [ "${1:-}" = "--apply" ] && APPLY=1
[ -w "$OC" ] || { echo "$OC not writable: run  sudo bash scripts/setup-linux-access.sh  once"; exit 1; }
VENDOR_KEEP="config.plist.nullmoth-20261010-040128"

list=()
[ -e "$OC/FSCK0000.000" ] && list+=("EFI/OC/FSCK0000.000")
for d in "$OC"/Kexts/*.nullmoth-*; do [ -e "$d" ] && list+=("EFI/OC/Kexts/${d##*/}"); done
# config backups, newest first by mtime; macOS writes FAT times in its own time zone (hours in the future seen
# from Linux), so a file dated after "now" sorts as older than every plausible Linux-dated file
mapfile -t cfg < <(cd "$OC" && find . -maxdepth 1 -name 'config.plist.*' -printf '%T@ %f\n' |
  awk -v now="$(date +%s)" '{ k = ($1 > now) ? 0 : 1; print k, $1, $2 }' | sort -k1,1nr -k2,2nr | awk '{print $3}')
n=0
for c in "${cfg[@]}"; do
  [ "$c" = "$VENDOR_KEEP" ] && continue
  n=$((n+1)); [ $n -le "$KEEP_CFG" ] && continue
  list+=("EFI/OC/$c")
done
mapfile -t logs < <(cd "$ESP" && ls -1 opencore-*.txt 2>/dev/null | sort -r)
for ((i=KEEP_LOGS; i<${#logs[@]}; i++)); do list+=("${logs[$i]}"); done
for f in "$ESP"/itlmt7925.*.old; do [ -e "$f" ] && list+=("${f##*/}"); done

[ ${#list[@]} -gt 0 ] || { echo "nothing to do"; exit 0; }
DEST=$HOME/ga403-esp-trash/$(date +%Y%m%d-%H%M%S)
for r in "${list[@]}"; do printf '%s  %s\n' "$( [ $APPLY = 1 ] && echo move || echo would-move )" "$r"; done
[ $APPLY = 1 ] || { echo "(dry run; --apply moves them to $DEST)"; exit 0; }
for r in "${list[@]}"; do
  mkdir -p "$DEST/$(dirname "$r")" && cp -a "$ESP/$r" "$DEST/$r" && rm -rf "$ESP/$r" || echo "FAILED: $r"
done
sync -f "$ESP"; echo "moved ${#list[@]} items to $DEST"
