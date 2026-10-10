#!/usr/bin/env bash
# One-time Linux host setup so the ESP and the macOS volume can be read WITHOUT sudo afterwards.
#
#   sudo bash scripts/setup-linux-access.sh          apply
#   sudo bash scripts/setup-linux-access.sh --undo   restore the saved /etc/fstab
#
# Changes (only /etc/fstab, backup kept as /etc/fstab.ga403-<time>):
#   /boot (ESP, vfat)   gains uid=<you>,gid=<you>; the mode stays umask=0077, so it is still private to you,
#                       but your user (and Claude running as you) can read/write it directly.
#   /mnt/macos-vol0     new "noauto,user,ro" APFS entry: `mount /mnt/macos-vol0` works as a normal user,
#                       read-only, Data volume (vol=0). Writing to APFS stays a root job
#                       (scripts/install-display-override.sh), by design.
# Not done on purpose: no NOPASSWD sudoers rule (anything that can write a script under your home would be root),
# no "disk" group (raw write access to every disk).
set -eu
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
U=${SUDO_UID:?run via sudo}; G=${SUDO_GID:?run via sudo}
FSTAB=/etc/fstab
if [ "${1:-}" = "--undo" ]; then
  b=$(ls -1t /etc/fstab.ga403-* 2>/dev/null | head -1) || true
  [ -n "${b:-}" ] || { echo "no backup found"; exit 1; }
  cp -a "$b" "$FSTAB" && echo "restored $b"; systemctl daemon-reload; umount /boot && mount /boot; exit 0
fi

cp -a "$FSTAB" "$FSTAB.ga403-$(date +%Y%m%d-%H%M%S)"
python3 - "$FSTAB" "$U" "$G" <<'PY'
import sys
path, u, g = sys.argv[1:4]
out, has_apfs = [], False
for line in open(path):
    f = line.split()
    if len(f) >= 4 and not line.lstrip().startswith("#"):
        if f[1] == "/boot" and f[2] == "vfat" and "uid=" not in f[3]:
            f[3] = "uid=%s,gid=%s,%s" % (u, g, f[3])
            line = "%-41s %-14s %-7s %-45s %s %s\n" % (f[0], f[1], f[2], f[3], f[4] if len(f) > 4 else "0", f[5] if len(f) > 5 else "2")
        if f[1] == "/mnt/macos-vol0":
            has_apfs = True
    out.append(line)
if not has_apfs:
    out.append("/dev/nvme0n1p4 /mnt/macos-vol0 apfs noauto,user,ro,vol=0,uid=%s,gid=%s 0 0\n" % (u, g))
open(path, "w").write("".join(out))
PY
mkdir -p /mnt/macos-vol0
systemctl daemon-reload
# vfat does not reliably re-read uid/gid on a remount, so do a real umount/mount
umount /boot && mount /boot
echo "owner of /boot: $(stat -c %U /boot)"
grep -E ' /boot | /mnt/macos-vol0 ' "$FSTAB"
echo "== done. As your normal user: ls /boot/EFI/OC ; mount /mnt/macos-vol0 ; umount /mnt/macos-vol0 =="
