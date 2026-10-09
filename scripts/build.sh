#!/bin/bash
# Build itlmt7925.kext on macOS without an Xcode project. Needs Xcode command line tools.
# Uses acidanthera/MacKernelSDK for the kernel headers/libs.
set -euo pipefail
cd "$(dirname "$0")/.."

SDK_DIR="${MACKERNELSDK:-build/MacKernelSDK}"
if [ ! -d "$SDK_DIR" ]; then
  mkdir -p build
  git clone --depth 1 https://github.com/acidanthera/MacKernelSDK "$SDK_DIR"
fi

OUT=build/itlmt7925.kext
rm -rf "$OUT"; mkdir -p "$OUT/Contents/MacOS"
cp Info.plist "$OUT/Contents/Info.plist"

clang++ -arch x86_64 -mmacosx-version-min=12.0 -std=c++17 -O2 -g \
  -mkernel -fapple-kext -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -nostdlib -ffreestanding \
  -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
  -I"$SDK_DIR/Headers" -Isrc \
  -Xlinker -kext -Xlinker -no_data_const \
  -L"$SDK_DIR/Library/x86_64" -lkmodc++ -lkmod -lcc_kext \
  -o "$OUT/Contents/MacOS/itlmt7925" src/itlmt7925.cpp

echo "built $OUT"
file "$OUT/Contents/MacOS/itlmt7925"
