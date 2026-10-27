#!/bin/bash
# Build and inspect a disposable kernel collection; never load or install it.
set -euo pipefail
PATH=/usr/bin:/bin:/usr/sbin:/sbin
[ "$(id -u)" = 0 ] || exit 1
D=$(mktemp -d /private/tmp/tuf-preflight.XXXXXX)
trap 'rm -rf "$D"' EXIT
mkdir -p "$D/repo"
for x in /Library/Extensions/*.kext; do
 [ -d "$x" ] || continue
 ditto "$x" "$D/repo/$(basename "$x")"
done
ditto /private/tmp/TufWmi.kext "$D/repo/TufWmi.kext"
chown -R root:wheel "$D/repo"
chmod -R go-w "$D/repo"
KARG=(--allow-missing-kdk)
[ ! -f /System/Library/Kernels/kernel ] || KARG+=(--kernel /System/Library/Kernels/kernel)
kmutil create -n aux --volume-root / "${KARG[@]}" -B /System/Library/KernelCollections/BootKernelExtensions.kc -S /System/Library/KernelCollections/SystemKernelExtensions.kc --repository "$D/repo" -A "$D/test.kc" -z > "$D/build.log" 2>&1 || { tail -30 "$D/build.log"; exit 1; }
kmutil inspect -a x86_64 -A "$D/test.kc" > "$D/inspect.txt" 2>&1
for id in local.tuf.fx517zm.wmi com.nullmoth.NVAccel com.nullmoth.NVRM com.nullmoth.NVRMFB com.nullmoth.NVRMAGDC; do
 grep -Fq "$id" "$D/inspect.txt" || { echo "Missing $id"; exit 1; }
done
shasum -a 256 /private/tmp/TufWmi.kext/Contents/MacOS/TufWmi | awk '{print $1}' > /private/tmp/tuf-preflight-sha256
cp "$D/build.log" /private/tmp/tuf-preflight-build.log
cp "$D/inspect.txt" /private/tmp/tuf-preflight-inspect.txt
chmod 644 /private/tmp/tuf-preflight-{sha256,build.log,inspect.txt}
echo 'PASS: TufWmi and all four NVIDIA drivers link. Installed collection unchanged.'
