# USB keyboard / mouse lag on AMD Strix Point (and what `ItlXhciKick` does about it)

## Symptom

On the reference machine (Ryzen AI 9 HX 370, four AMD xHCI controllers `1022:151e`, `151f`, `151a`, `151b`) macOS 15 showed:

* USB keyboards and mice react, but a key release arrives about two seconds late: a tap on `d` types `ddddd`, keys feel "sticky".
* The I2C trackpad (VoodooI2C) is smooth, so it is not CPU load, HID, WindowServer or the clock.
* Disabling all Voodoo kexts did not change it, so it is not the I2C stack either.

## Measurement

`ItlDiag` (boot-arg `itlxr=2`) samples each controller's event rings from outside the driver and records, for ring 1, how
long an event sits between "controller wrote it" and "macOS consumed it":

```
before: ring=1  late5 ~ 2060 ms   maxlat ~ 2060 ms        (one consumption every 2.048 s)
after : ring=1  ev=8284 tx=8284 late5=1 maxlat=5ms        (x1 = the controller with the keyboard)
```

2.048 s is exactly one wrap of the xHCI `MFINDEX` register (14 bits of 125 µs micro-frames). Interrupter 0 raises an
interrupt when `MFINDEX` wraps, and the stale events were only consumed then.

## Cause (read from macOS 15.6.1 (24G90) Recovery, `BootKernelExtensions.kc`)

All of it comes from disassembling Apple's own `AppleUSBXHCI` / `AppleUSBXHCIPCI` in the Recovery image (method in the
last section), cross-checked with the xHCI 1.2 specification and the IOPCIFamily sources:

1. `AppleUSBXHCI::start` requires the controller to report at least 3 interrupters and always programs **three**:
   ring 0 for commands, port-status changes and `MFINDEX` wrap, **ring 1 for the transfer events of every
   non-isochronous endpoint (that is: keyboards, mice, storage, hubs)**, ring 2 for isochronous endpoints.
2. `AppleUSBXHCIPCI::start` creates exactly **one** `IOFilterInterruptEventSource`, on the first MSI vector of the
   device. Its filter calls the three per-interrupter filters and signals `interruptOccurred`, which drains all three
   rings unconditionally (there is no IP/EINT test) and the driver also has a `manuallyProcessInterrupts` entry.
3. The Strix Point controllers expose **one** MSI / MSI-X vector (`MaxIntrs = 8` but a single message). Measured behaviour:
   events queued on interrupter 1 do not raise that vector; only interrupter 0 does. Whether this is the silicon, the
   firmware or an enable bit macOS never sets for interrupter 1 was not determined.

Result: ring-1 events are only picked up when interrupter 0 happens to fire, i.e. every 2.048 s. Linux's xhci driver
puts all events on the primary interrupter, so one vector is enough there; Apple's driver depends on the secondary one.

Ideas that came up on the way and do not address the cause:

| Idea | Result |
|---|---|
| `IOPCIMSIMode = false` in `DeviceProperties` to force legacy INTx | no effect: in IOPCIFamily (`IOPCIMessagedInterruptController.cpp`) that property is an **output**, set when MSI is enabled and removed when it is disabled. No property forces INTx either |
| `XhciPortLimit`, port maps, USB power SSDTs | the latency is in interrupt delivery, not in enumeration or power |
| disabling the Voodoo kexts / the trackpad | the trackpad is I2C, not USB; the symptom stayed |
| raising the keyboard repeat delay (`ItlDiag` boot-arg `itlkbd=<ms>`) | masks the repeating characters, does not remove the 2 s latency; kept only as a fallback |
| reading `IP` / `EHB` from outside to prove the missing interrupt | inconclusive: `IP` is cleared by hardware right after the MSI write, so `ip=0` proves nothing |

## Fix: `ItlXhciKick.kext`

Every millisecond, for each AMD xHCI controller, take the controller's work-loop gate and call the public
`IOFilterInterruptEventSource::signalInterrupt()` on the event source that Apple's driver created for that PCI device.
That runs Apple's own `interruptOccurred`, which drains ring 1 (and 2) through Apple's code.

* No register is written, no Apple code is patched; only public IOKit API is used (`IOWorkLoop::runAction`,
  `signalInterrupt`). The event source is found by walking the work loop's event chain under the gate and matching
  `getProvider()` against the xHCI's `IOPCIDevice`, so a controller that disappears cannot leave a dangling pointer.
* Controllers are found by `serviceMatching("AppleUSBXHCIPCI")`, filtered to vendor `0x1022` (`itlkickall=1` removes the
  filter). First kick 3 s after discovery.
* Boot-args: `itlkick=<µs>` period (default 1000, minimum 200, `0` = off), `itlkickall=1`.
* Statistics in NVRAM variable `itlz0` for the first two minutes every 10 s, then every minute:
  `kick up=178s p=1000us c0:157242/157242/0 c1:157242/157242/0 ...` = `kicks/hits/miss` per controller.
  `hits == kicks` means the event source was found on every pass; `hits = 0` would mean it was not found (then nothing
  is fixed and the kext only burns a little CPU).

Cost: 1000 wake-ups per second per controller, 4000/s on this machine. CPU use has not been measured. A larger period
(`itlkick=2000`) halves it at the price of up to 2 ms extra latency.

## Verified

Reference machine, Recovery, `itlkick` default: all four controllers 157242 kicks / 157242 hits / 0 misses after 178 s;
ring-1 maximum latency 5 ms instead of ~2060 ms; keyboard and mouse typing normal (reported by the user, not
measured with a key-event timestamp tool). Not tested: other machines, other AMD generations, sleep/wake, an installed system.

## Reproducing the analysis

```bash
# 7-Zip can read the HFS+ image inside the Recovery stick (com.apple.recovery.boot/BaseSystem.dmg)
7z x BaseSystem.dmg "*BootKernelExtensions.kc" -r
pip install capstone
```

`tools/kc.py` parses the kernel collection (Mach-O fileset), builds a symbol table across all kexts, finds string
cross-references and disassembles functions with Capstone. Typical session:

```python
import os; os.environ["KC"] = "macOS Base System/System/Library/KernelCollections/BootKernelExtensions.kc"
import sys; sys.path.insert(0, "tools"); import kc
start = next(a for a, n in kc.syms.items() if n == "__ZN15AppleUSBXHCIPCI5startEP9IOService")
lo, hi = kc.func_range(start)
kc.disasm(lo, hi)                                   # annotated with string and symbol references
print(kc.xrefs(kc.find_string_va("IOPCIMSIMode")))  # code that references a string: {string va: [instruction va]}
```

Addresses differ per macOS build; look functions up by symbol name (`kc.syms`).
