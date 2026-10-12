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

## Result log

* 2026-10-12, stage A, commit 5dca5d5: native kext did not load. Kernel: `library kext com.itlmt7925.kext not compatible with
  requested version 0.1.0` (0xdc00800e), because the core Info.plist had no `OSBundleCompatibleVersion`, so it was not a library
  kext. Native `start` was never reached; the Wi-Fi loss in that boot was **not** caused by the native code (cause not established;
  `itlmt_fast=6` and `nvrmacpibl=1` were also new in boot-args). Fix: core Info.plist now has `OSBundleCompatibleVersion`; `install`
  patches the ESP copy of the core Info.plist in place (credentials stay), no need to replace the core kext.
* Same boot, the Wi-Fi loss itself (core driver, native never loaded): scan works, then every join dies with
  `efuse hw type failed rc=-5` -> `hw-prep-fail` (`itlmt7925_pwr.cpp` efuseRead, the power-table code of a45e8c1 had never run on
  hardware). Fix: power tables are best effort (failure is logged and the join continues), and the rejected efuse event is logged
  (`efuse event rejected: len= tag= addr= want= valid=`) so the parser can be corrected from a real event. This needs a rebuilt
  **core** kext (replace it on the ESP, then re-inject the credentials with `scripts/inject-creds.ps1`).
* 2026-10-12, stage A, commit 9339299 (native kext now links): **kernel panic at 0.47 s uptime**, before the filesystem is up
  (decoded from `AAPL,PanicInfo0000..0007`, 7-bit unpacking, in the user's tarball). Page fault CR2=RSI=RDI=0x648 (RBX=0) in
  `IOLockLock+0x3`, called from `IONetworkController::start+0x1d4` <- `IO80211Controller::start+0x41` (the `call [rax+0x5d0]`
  to the IONetworkController start) <- `itlmt7925Wifi::start+0x8c` (our `super::start`). So the work queue, CoreCapture logger and
  fault reporter built before it did not fault; `IONetworkController::start` locks a lock inside an object it holds as NULL
  (`lock = NULL + 0x648`). Which pointer is NULL is **chưa xác định** (candidates: a field `IONetworkController::init` fills, the
  provider being the core driver and not a PCI device, a global of IONetworkingFamily not ready at 0.47 s). Rolled back by the
  user (native `Enabled=False`, `itlmt_native*` removed). **Cause found from the round-6 dump** (`abi/native-wifi/24H32/round6/`):
  the `IOLockLock` is inside `ml_thread_policy(thread, 1, 0x10000001)`, and `thread` is NULL. `IONetworkController::start`
  (+0x1a0..0x1d4) compares `this->getWorkLoop()` with `provider->getWorkLoop()`; when they differ it calls
  `ml_thread_policy(workLoop->getThread(), ...)`. Our `getWorkLoop()` was `IO80211Controller`'s, which returns something other than
  the provider's loop, and `IO80211WorkQueue::getThread` (0xffffff80022591fa, 8 bytes between two method addresses) is a stub that
  returns NULL, so the call faulted at `thread+0x648`. Round 7 (`abi/native-wifi/24H32/round7/`): `IO80211Controller::getWorkLoop` is a pure tail call to virtual
  `getWorkQueue` (slot 399; Family default returns NULL, BCM returns `ivars+0x7898`), and `IO80211WorkQueue::getThread` is
  `xor eax,eax`. So Apple's BCM also returns an `IO80211WorkQueue` from `getWorkLoop`; it avoids the policy call only if its provider
  (the bus layer, which supplies that queue by default, see `createWorkQueue`) returns the *same* queue from `getWorkLoop`, so the
  compare matches. That is an inference (the bus class was not dumped): **chưa xác định**. Our provider is the core `itlmt7925`
  (plain `IOService::getWorkLoop` = the PCI device's loop), so the equal-loops condition is met by returning the provider's loop. Fix (commit after 4435061): `itlmt7925Wifi::getWorkLoop()` returns the
  provider's work loop (fallback: an own `IOWorkLoop`), so the compare matches and `ml_thread_policy` is skipped. Slots 20/0 and
  compile check pass. **Not run on hardware.** Residual risk: code in the IO80211 family that casts `getWorkLoop()` to an
  `IO80211WorkQueue` (none seen in the dumps read so far) and any further fault later in `IONetworkController::start` (after the policy call it creates an
  `IOCommandGate` and `addEventSource`s it to the loop returned by `getWorkLoop()`, i.e. now the provider's loop, which must stay
  enabled/running for the life of the controller).
* 2026-10-12, stage A, commit b6693cb (`docs/native-wifi-results/20261012-stage-a-b6693cb/`): **PASS, no panic.** `com.itlmt7925.native`
  loaded, `itlmt7925Wifi` registered/matched/active, `kern.bootargs` had `itlmt_native=1` only. Confirms the `getWorkLoop()` fix
  (`ml_thread_policy` path skipped, command gate on the provider's loop did not fault). The `controller started` IOLog line was not seen
  (registry entry is the evidence). Not tested: stage B (interface), sleep/wake, unload. The core joined and got DHCP (`RESULT rc=0
  link up`) without power tables: `efuse event rejected: len=124 tag=0002 addr=00000a70 want=00000a70 valid=00000001`. The event is the
  expected shape (address and valid match) but its tag is **2**, the parser demanded 1. Fix: `efuseRead` accepts tag 1 or 2 and logs
  the first 8 bytes of the block (`efuse a71 tag= block=`). First run of the CLC and tx-power commands on hardware follows from this,
  still best effort (failure is logged, join continues). Whether tag 2 puts the 16-byte block at the same offset is **chưa xác định**
  until the logged block is checked against the known encapsulation byte. Needs a rebuilt core kext.
