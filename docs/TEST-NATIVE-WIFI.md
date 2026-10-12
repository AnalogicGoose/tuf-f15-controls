# First load of the native Wi-Fi kext (`itlmt7925Native`)

Status: nothing in the native path has ever run. Stage 1 (controller) and stage 2 (interface + queues) are verified only up to
build, link check and vtable slots (HANDOFF-NATIVE-WIFI.md). Treat this as an experiment with a way back, not as an update.

## 0. Before you start

* Have a stick with a working copy of the current EFI (or `scripts/native-wifi-test.sh backup` output copied to it) and know how
  to boot from it (OpenCore picker, F8/Esc on the ASUS). A panic with `debug=0x100` in the boot-args halts instead of rebooting.
* Know that the box is offline for Wi-Fi if the experiment fails: keep the USB Ethernet / phone tether nearby.
* `nvram boot-args` shows what NVRAM overrides; if it is non-empty it wins over `config.plist` (the helper stores it in the backup).

## 1. Build

On the Mac, in the worktree: `bash scripts/build.sh itlmt7925 itlmt7925Native` (build/ contains both kexts), then
`KC=<path to the .kc> python3 tools/check_kext_links.py build/itlmt7925Native.kext build/itlmt7925.kext` must exit 0 with only
the 6 baseline-missing `IONetworkController` symbols. The native kext must be the build from the same commit as the core kext.

## 2. Backup (done automatically by install, can be run alone)

    scripts/esp-automount.sh          # mounts the ESP at /Volumes/ESP (password box)
    scripts/native-wifi-test.sh backup

Creates `~/itlmt-backup/EFI-<time>/` (+ `.tar.gz`, + `.nvram-boot-args.txt`). Copy the `.tar.gz` to the stick too.

## 3. Stage A: controller only (lowest risk)

    scripts/native-wifi-test.sh install build/itlmt7925Native.kext

This backs up again, copies the kext, adds it to `Kernel/Add` right after `itlmt7925.kext` and appends **both** boot-args. For
stage A edit `config.plist` by hand afterwards and remove `itlmt_native_if=1` (only `itlmt_native=1`). Reboot.

Expected: boots; the Ethernet-style Wi-Fi from the core kext is replaced by the native controller, no Wi-Fi in System Settings
yet (no interface). Collect: `scripts/native-wifi-test.sh logs`. Fail = panic or hang on boot, or the core kext lost its network.

## 4. Stage B: controller + interface

Add `itlmt_native_if=1` back to `boot-args` (`itlmt_native_unit=N` is optional; default 9) and reboot. Look for:

* `kextstat | grep itlmt` shows both kexts;
* `ioreg -l | grep -i itlmt7925Wifi` shows the controller with a child interface (class `itlmt7925WifiInterface`);
* `ifconfig -a` has a new `en9`; System Settings > Network may list a Wi-Fi service (it will not connect: every handler answers
  unsupported, the radio is not driven yet).

A clean failure is also useful: the log line says which step of `makeInterface`/`startWithInfo` returned false.

## 5. Sleep / unload (only if B loaded)

Sleep and wake once; then reboot normally (do not unload by hand). Report whether either panics: teardown order is the least
verified part.

## 6. Go back

    scripts/native-wifi-test.sh restore            # newest backup; or: restore ~/itlmt-backup/EFI-<time>

Reboot. If the native boot-args stick: OpenCore picker > Reset NVRAM, or boot from the stick. By hand: remove
`itlmt7925Native.kext` from `EFI/OC/Kexts` and its `Kernel/Add` entry, and delete `itlmt_native=1 itlmt_native_if=1`.
A missing `boot-args` token alone makes the native kext inert (its `start` returns false), the core driver then works as before.

## 7. What to send back

`scripts/native-wifi-test.sh logs` output (kextstat, ioreg, `log show --last boot`, dmesg; remove SSIDs/MACs), the stage reached
(A/B/5), and for a panic the first 30 lines of the panic text (photo is fine). The ItlDiag `ITLLOG.BIN` on the ESP also has the
early log (`scripts/usblog.ps1 -Esp -Read` on Windows).
