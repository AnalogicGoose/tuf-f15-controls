#!/usr/bin/env bash
# Install (or remove) the 120 Hz display override on the macOS Data volume from Linux. Run with sudo.
#
#   sudo bash scripts/install-display-override.sh          install
#   sudo bash scripts/install-display-override.sh --undo   remove the override again
#
# What it changes on the macOS Data volume (APFS volume 0, mounted read-write for the duration):
#   + /Library/Displays/Contents/Resources/Overrides/DisplayVendorID-4c83/DisplayProductID-419c
#   - /Library/Preferences/com.apple.windowserver.plist and every user's ByHost/com.apple.windowserver.*.plist
#     (they pin mode 0x10000 = 60 Hz, which forces a 120 -> 60 Hz modeset at login; WindowServer rebuilds them)
# The removed prefs are copied to ~/ga403-macos-backup-<time>/ first. Nothing else is touched.
# Write support comes from linux-apfs-rw (experimental): the script refuses to continue if the volume does not
# mount read-write, and runs a read-only apfsck afterwards.
set -u
UNDO=0; [ "${1:-}" = "--undo" ] && UNDO=1
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
HERE=$(cd "$(dirname "$0")/.." && pwd)
SRC="$HERE/macos/display-override/DisplayVendorID-4c83/DisplayProductID-419c"
APFS_DEV=${APFS_DEV:-/dev/nvme0n1p4}
MP=/mnt/macos-vol0
OWNER=${SUDO_USER:-root}
OWNER_HOME=$(getent passwd "$OWNER" | cut -d: -f6)
BK="$OWNER_HOME/ga403-macos-backup-$(date +%Y%m%d-%H%M%S)"
OVR_REL="Library/Displays/Contents/Resources/Overrides/DisplayVendorID-4c83"

[ $UNDO = 1 ] || [ -f "$SRC" ] || { echo "missing $SRC"; exit 1; }
modprobe apfs || { echo "modprobe apfs failed"; exit 1; }

echo "== 1. unmount every APFS mount of $APFS_DEV =="
for m in /mnt/macos-vol[0-9]*; do mountpoint -q "$m" && umount "$m" && echo "umount $m"; done
mountpoint -q /mnt/macos-vol0 && { echo "could not unmount $MP"; exit 1; }

echo "== 2. mount the Data volume read-write =="
mkdir -p "$MP"
if ! mount -t apfs -o rw,vol=0 "$APFS_DEV" "$MP"; then
  echo "rw mount failed (volume not cleanly unmounted, or no write support). dmesg:"; dmesg | tail -5
  mount -t apfs -o ro,vol=0,uid="${SUDO_UID:-1000}",gid="${SUDO_GID:-1000}" "$APFS_DEV" "$MP"
  echo "NOTHING was changed. Boot macOS once (or in recovery) and shut it down cleanly, then retry."
  exit 1
fi
grep -q " $MP .*[ ,(]rw[ ,)]" /proc/mounts || { echo "mounted, but not rw"; umount "$MP"; exit 1; }
[ -d "$MP/Library/Preferences" ] || { echo "$MP does not look like the macOS Data volume"; umount "$MP"; exit 1; }

if [ $UNDO = 1 ]; then
  echo "== 3. remove the override =="
  rm -rfv "$MP/$OVR_REL"
else
  echo "== 3. back up and remove the pinned WindowServer prefs (-> $BK) =="
  mkdir -p "$BK"
  for f in "$MP"/Library/Preferences/com.apple.windowserver.plist \
           "$MP"/Users/*/Library/Preferences/ByHost/com.apple.windowserver.*.plist; do
    [ -f "$f" ] || continue
    d="$BK/${f#$MP/}"; mkdir -p "$(dirname "$d")"
    cp -a "$f" "$d" && rm -v "$f"
  done
  chown -R "$OWNER:" "$BK"

  echo "== 4. install the override =="
  install -d -m 755 -o 0 -g 0 \
    "$MP/Library/Displays" "$MP/Library/Displays/Contents" "$MP/Library/Displays/Contents/Resources" \
    "$MP/Library/Displays/Contents/Resources/Overrides" "$MP/$OVR_REL"
  install -m 644 -o 0 -g 0 "$SRC" "$MP/$OVR_REL/DisplayProductID-419c"
  ls -la "$MP/$OVR_REL"
fi

echo "== 5. flush, unmount, read-only consistency check =="
sync
umount "$MP" || { echo "umount failed"; exit 1; }
if command -v apfsck >/dev/null; then
  apfsck "$APFS_DEV" && echo "apfsck: clean" || echo "apfsck reported problems (see above); do NOT boot macOS before looking at it"
else
  echo "apfsck not installed (apfsprogs), skipped"
fi
mount -t apfs -o ro,vol=0,uid="${SUDO_UID:-1000}",gid="${SUDO_GID:-1000}" "$APFS_DEV" "$MP" && echo "remounted $MP read-only"
echo "== done =="
