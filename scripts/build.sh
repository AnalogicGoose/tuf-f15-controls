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

COMMON="-arch x86_64 -mmacosx-version-min=12.0 -O2 -g -mkernel -fapple-kext -fno-builtin -fno-stack-protector -nostdlib -ffreestanding -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -I$SDK_DIR/Headers -Isrc"
mkdir -p build/obj
clang $COMMON -c src/kmod_info.c -o build/obj/kmod_info.o
clang++ $COMMON -std=c++17 -fno-exceptions -fno-rtti -c src/itlmt7925.cpp -o build/obj/itlmt7925.o
clang++ $COMMON -std=c++17 -fno-exceptions -fno-rtti -c src/itlmt7925_m1.cpp -o build/obj/itlmt7925_m1.o
clang++ $COMMON -std=c++17 -fno-exceptions -fno-rtti -c src/itlmt7925_m2.cpp -o build/obj/itlmt7925_m2.o
clang++ $COMMON -std=c++17 -fno-exceptions -fno-rtti -c src/itlmt7925_m3.cpp -o build/obj/itlmt7925_m3.o
clang++ $COMMON -std=c++17 -fno-exceptions -fno-rtti -c src/itl_wpa.cpp -o build/obj/itl_wpa.o
clang $COMMON -c src/fw_blob.c -o build/obj/fw_blob.o     # embeds fw/*.bin, path relative to repo root
clang++ -arch x86_64 -mmacosx-version-min=12.0 -nostdlib -Xlinker -kext -Xlinker -no_data_const   -L"$SDK_DIR/Library/x86_64" -lkmodc++ -lkmod -lcc_kext   build/obj/kmod_info.o build/obj/itlmt7925.o build/obj/itlmt7925_m1.o build/obj/itlmt7925_m2.o build/obj/itlmt7925_m3.o build/obj/itl_wpa.o build/obj/fw_blob.o   -o "$OUT/Contents/MacOS/itlmt7925"

echo "built $OUT"
file "$OUT/Contents/MacOS/itlmt7925"

echo "--- undefined symbols"
nm -um "$OUT/Contents/MacOS/itlmt7925" || true
echo "--- load commands"
otool -l "$OUT/Contents/MacOS/itlmt7925" | grep -E "segname|sectname|cmd " | head -60 || true
echo "--- kextlibs"
kextlibs -xml -all-symbols "$OUT" 2>&1 | head -40 || true

# ---- ItlDiag (boot-time PCI/USB dump) ----
D=build/ItlDiag.kext
rm -rf "$D"; mkdir -p "$D/Contents/MacOS"
cp diag/Info.plist "$D/Contents/Info.plist"
clang $COMMON -c diag/kmod_info.c -o build/obj/diag_kmod_info.o
clang++ $COMMON -std=c++17 -fno-exceptions -fno-rtti -c diag/ItlDiag.cpp -o build/obj/ItlDiag.o
clang++ -arch x86_64 -mmacosx-version-min=12.0 -nostdlib -Xlinker -kext -Xlinker -no_data_const \
  -L"$SDK_DIR/Library/x86_64" -lkmodc++ -lkmod -lcc_kext \
  build/obj/diag_kmod_info.o build/obj/ItlDiag.o -o "$D/Contents/MacOS/ItlDiag"
file "$D/Contents/MacOS/ItlDiag"
