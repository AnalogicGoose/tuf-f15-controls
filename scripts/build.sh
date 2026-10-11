#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# Builds every kext of this repo on macOS without an Xcode project. Needs the Xcode command line tools.
# Kernel headers/libs come from acidanthera/MacKernelSDK (cloned into build/ on first use, or set MACKERNELSDK).
#
#   scripts/build.sh            build itlmt7925, ItlXhciKick, ItlDiag into build/*.kext
#   scripts/build.sh itlmt7925  build only the named kext(s)
#
# The MediaTek firmware in fw/ is embedded into itlmt7925 with .incbin (kexts/itlmt7925/fw_blob.c), so this script must
# run with the repo root as working directory (it does: it cd's there).
set -euo pipefail
cd "$(dirname "$0")/.."

SDK_DIR="${MACKERNELSDK:-build/MacKernelSDK}"
if [ ! -d "$SDK_DIR" ]; then
  mkdir -p build
  git clone --depth 1 https://github.com/acidanthera/MacKernelSDK "$SDK_DIR"
fi

COMMON="-arch x86_64 -mmacosx-version-min=12.0 -O2 -g -mkernel -fapple-kext -fno-builtin -fno-stack-protector -nostdlib -ffreestanding -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -I$SDK_DIR/Headers"
CXXFLAGS="-std=c++17 -fno-exceptions -fno-rtti"
OBJ=build/obj
mkdir -p "$OBJ"

# build_kext <name> <source dir> <source files...>
# Bundle layout: build/<name>.kext/Contents/{Info.plist,MacOS/<name>}
build_kext() {
  local name="$1" dir="$2"; shift 2
  local out="build/$name.kext" objs=()
  rm -rf "$out"; mkdir -p "$out/Contents/MacOS"
  cp "$dir/Info.plist" "$out/Contents/Info.plist"
  for src in "$@"; do
    local o="$OBJ/${name}_$(basename "${src%.*}").o"
    case "$src" in
      *.cpp) clang++ $COMMON $CXXFLAGS -I"$dir" -c "$dir/$src" -o "$o" ;;
      *.c)   clang   $COMMON -I"$dir" -c "$dir/$src" -o "$o" ;;
    esac
    objs+=("$o")
  done
  clang++ -arch x86_64 -mmacosx-version-min=12.0 -nostdlib -Xlinker -kext -Xlinker -no_data_const \
    -L"$SDK_DIR/Library/x86_64" -lkmodc++ -lkmod -lcc_kext "${objs[@]}" -o "$out/Contents/MacOS/$name"
  echo "built $out"
  file "$out/Contents/MacOS/$name"
  echo "--- undefined symbols ($name)"
  nm -um "$out/Contents/MacOS/$name" || true
}

want() { local n="$1"; shift; [ $# -eq 0 ] && return 0; for a in "$@"; do [ "$a" = "$n" ] && return 0; done; return 1; }
TARGETS=("$@")

want itlmt7925 "${TARGETS[@]+"${TARGETS[@]}"}" && build_kext itlmt7925 kexts/itlmt7925 \
  kmod_info.c itlmt7925.cpp itlmt7925_m1.cpp itlmt7925_m2.cpp itlmt7925_m3.cpp itlmt7925_eth.cpp itlmt7925_ba.cpp itlmt7925_uc.cpp itl_wpa.cpp itl_caps.cpp itl_ba.cpp fw_blob.c
want ItlXhciKick "${TARGETS[@]+"${TARGETS[@]}"}" && build_kext ItlXhciKick kexts/ItlXhciKick kmod_info.c ItlXhciKick.cpp
want ItlDiag "${TARGETS[@]+"${TARGETS[@]}"}" && build_kext ItlDiag kexts/ItlDiag kmod_info.c ItlDiag.cpp
want AmdCppc "${TARGETS[@]+"${TARGETS[@]}"}" && build_kext AmdCppc kexts/AmdCppc kmod_info.c AmdCppc.cpp
exit 0
