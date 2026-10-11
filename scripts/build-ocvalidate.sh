#!/usr/bin/env bash
# Build ocvalidate for the OpenCore commit the ESP's OpenCore.efi was built from, no macOS needed.
#
#   bash scripts/build-ocvalidate.sh            # -> ~/.cache/ga403/bin/ocvalidate
#   ~/.cache/ga403/bin/ocvalidate /boot/EFI/OC/config.plist
#
# Why a build: the OpenCore on the ESP is a dev build ("DBG-109", 1.0.9 + patches/opencore/fix-zen5-fsb.diff), and the
# last release's ocvalidate (1.0.8) does not know the 1.0.9 keys. ocvalidate prints "only compatible with 1.0.9" when it matches.
# Downloads (ask first): OpenCorePkg at the pinned commit (~60 MB shallow) and acidanthera/audk's MdePkg, MdeModulePkg and
# UefiCpuPkg (sparse, ~60 MB). Needs git, make, gcc.
set -eu
COMMIT=${OC_COMMIT:-963025f1382eca619412d527ad82b6d19bee70d9}      # same default as .github/workflows/opencore.yml
C=${CACHE:-$HOME/.cache/ga403}; SRC=$C/OpenCorePkg; UDK=$C/UDK; BIN=$C/bin
mkdir -p "$C" "$BIN"
if [ "$(git -C "$SRC" rev-parse HEAD 2>/dev/null)" != "$COMMIT" ]; then
  [ -d "$SRC/.git" ] || { mkdir -p "$SRC"; git -C "$SRC" init -q; git -C "$SRC" remote add origin https://github.com/acidanthera/OpenCorePkg; }
  git -C "$SRC" fetch -q --depth 1 origin "$COMMIT"; git -C "$SRC" checkout -q FETCH_HEAD
fi
if [ ! -d "$UDK/MdeModulePkg/Library/CommonMemoryAllocationLib" ]; then
  rm -rf "$UDK"
  git clone -q --depth 1 --filter=blob:none --sparse https://github.com/acidanthera/audk "$UDK"
  git -C "$UDK" sparse-checkout set MdePkg MdeModulePkg UefiCpuPkg
fi
make -C "$SRC/Utilities/ocvalidate" -j"$(nproc)" UDK_PATH="$UDK" >/dev/null
install -m755 "$SRC/Utilities/ocvalidate/ocvalidate" "$BIN/ocvalidate"
echo "$BIN/ocvalidate"
