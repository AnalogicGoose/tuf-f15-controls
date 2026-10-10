# Reference EFI: ASUS ROG Zephyrus G14 (GA403WM), Ryzen AI 9 HX 370

The OpenCore configuration this project was developed and tested with. It boots the **macOS 15.6.1 (24G90) Recovery** image
from a USB stick with OpenCore 1.0.9. A full macOS install on the internal disk has not been tried with it.

| | |
|---|---|
| Machine | ASUS ROG Zephyrus G14 GA403WM, Ryzen AI 9 HX 370 (Strix Point / Zen 5), Radeon 890M, RTX 5060 Laptop GPU |
| Wi-Fi | MediaTek MT7925, PCI `14c3:7925` (driver: `itlmt7925.kext`) |
| USB | four AMD xHCI controllers `1022:151e`, `151f`, `151a`, `151b` (fix: `ItlXhciKick.kext`) |
| Trackpad / keyboard | I2C-HID `093a:3011` via VoodooI2C; USB keyboard `0b05:19b6` |
| OpenCore | 1.0.9 built from `963025f1382eca619412d527ad82b6d19bee70d9` with `patches/opencore/fix-zen5-fsb.diff` |

## What is in this folder

```
efi/
  OC/config.plist       the configuration, PlatformInfo identifiers replaced by placeholders
  OC/ACPI/*.aml         the SSDTs referenced by config.plist
  kexts.json            third-party kexts used (bundle id, version, SHA-256); see KEXTS.md
  KEXTS.md              what each kext is for and where to get it
```

Not included, on purpose: OpenCore binaries, OpenCore drivers, third-party kexts, `Resources/`. Download them from their
projects; the versions are listed in `KEXTS.md`, the OpenCore patch is built by the `opencore` GitHub workflow.

## Before the first boot

1. **Generate your own SMBIOS.** `PlatformInfo > Generic` contains `CHANGE_ME_SN`, `CHANGE_ME_MLB_000`, an all-zero
   `SystemUUID` and an all-zero `ROM`. Replace them with values you generate (`macserial`, GenSMBIOS). Never publish them.
   The reference configuration uses `SystemProductName = MacBookPro16,2`, `UpdateSMBIOSMode = Custom`.
2. **CPU core count.** The four `Force cpuid_cores_per_package to constant` kernel patches contain the core count in their
   `Replace` bytes: `0x0c` = 12 cores (Ryzen AI 9 HX 370). Change it for another CPU.
3. **Assemble the folder.** `EFI/OC/` = this `config.plist` + `ACPI/` + the kexts of `KEXTS.md` in `Kexts/` + `Drivers/`
   (`HfsPlus.efi`, `OpenRuntime.efi`, `ResetNvramEntry.efi`) + the patched `OpenCore.efi`. Add the three kexts of this
   repository (`itlmt7925`, `ItlXhciKick`, optionally `ItlDiag`) from a release or from `scripts/build.sh`.
4. **Wi-Fi credentials** go into the personality of `itlmt7925.kext` (`docs/WIFI.md`). They are stored in plain text on the
   stick; do not share the stick or that plist.
5. Validate with `ocvalidate` from the same OpenCore release. It will complain about the placeholders until you replace them.

## What the configuration does

* **ACPI.** `SSDT-EC` (fake EC for macOS), `SSDT-PLUG-ALT` (`plugin-type` on the CPU objects), `SSDT-RMNE` (NullEthernet
  device), `SSDT-USB-Reset` (`_STA` of the four root hubs under Darwin), `SSDT-USBX` (USB power properties),
  `SSDT-XOSI` (+ the `_OSI` to `XOSI` rename: answers "Windows" when macOS asks).
  `SSDT-Disable_Network_GPP5` hides the Wi-Fi card from macOS; it is **disabled** here so that `itlmt7925.kext` can see it.
* **Kernel.** AMD vanilla kernel patches (cpuid, `rdmsr`, PAT, non-monotonic time panic) as shipped by the base
  configuration, plus the two IOPCIFamily `probeBusGated` "disable 10 bit tags" patches. `ProvideCurrentCpuInfo`,
  `DummyPowerManagement` and `ForgedInvariant.kext` handle CPU info/TSC on AMD.
* **Booter.** `RebuildAppleMemoryMap`, `SyncRuntimePermissions`, `DevirtualiseMmio`, `EnableSafeModeSlide`,
  `ProvideCustomSlide`, `AvoidRuntimeDefrag`, `AllowRelocationBlock`.
* **Security.** `SecureBootModel = Disabled`, `Vault = Optional`, `DmgLoading = Signed`, `ScanPolicy = 0` (everything is
  shown by the picker). Test configuration, not a hardened one.
* **Debug.** `Misc > Debug` is verbose (`Target 67`, `AppleDebug`, `SysReport`). Switch it off for daily use.

### boot-args (reference machine)

```
-v debug=0x100 keepsyms=1 -vi2c-force-polling nvfb=1 nvaccel=1 nvfbheads=4 -nvkmsnosmooth
amfi_get_out_of_my_way=0x1 amfi=0x80 itldiag=90 itlxr=2 itlmt_m1=75 itlmt_m2=1 itlmt_m3=1 msgbuf=1048576
```

| Argument | Meaning |
|---|---|
| `-v`, `debug=0x100`, `keepsyms=1` | verbose boot; keep panic text on screen; keep kernel symbols in panic logs |
| `-vi2c-force-polling` | VoodooI2C polls the trackpad instead of using GPIO interrupts |
| `nvfb=1 nvaccel=1 nvfbheads=4 -nvkmsnosmooth` | inherited from the base configuration's NVIDIA driver build; unrelated to Wi-Fi / USB |
| `amfi_get_out_of_my_way=0x1 amfi=0x80` | disables AMFI; inherited from the base configuration |
| `itlmt_m1=75 itlmt_m2=1 itlmt_m3=1` | itlmt7925: start after 75 s, scan, connect (`docs/WIFI.md`) |
| `itldiag=90 itlxr=2` | ItlDiag diagnostics and xHCI event-ring sampler (`docs/DEBUGGING.md`); not needed for normal use |
| `msgbuf=1048576` | larger kernel message buffer, so the kext logs are not truncated |
| `itlkick=<us>` (not set) | ItlXhciKick period; default 1000 µs, `0` disables (`docs/USB-XHCI-LAG.md`) |

### Known leftovers (harmless, not removed because the configuration is published exactly as tested)

* `DeviceProperties > Add` has `IOPCIMSIMode = false` on the four xHCI controllers. It does nothing: IOPCIFamily *sets*
  that property when it enables MSI and never reads it back (`docs/USB-XHCI-LAG.md`). It can be deleted.
* `ItlDiag.kext` and the `itldiag` / `itlxr` boot-args are debugging tools.
* The `nv*` boot-args and the NVIDIA-related entries of the base configuration are unused here.

## The OpenCore patch (`patches/opencore/fix-zen5-fsb.diff`)

Stock OpenCore 1.0.9 computes the wrong `FSBFrequency` on Zen 5 (family 1Ah): the 12-bit FID from `PStateDef0` (400 =
2000 MHz on the HX 370) was narrowed to `UINT8` before being scaled, wrapped to 208 and gave an FSB of 9.6 MHz instead of
100 MHz. XNU derives its LAPIC timer conversions from that value, so timers fired about ten times too early. The patch keeps
the FID at 12 bits and derives the bus ratio from the measured TSC, as the 17h/19h path does. The `opencore` workflow
builds OpenCore from the exact upstream commit with the patch applied (manual trigger, artifact `opencore-zen5-fsb`).
`ItlDiag`'s `itlt1` variable shows the effect: `ratio=995/1000` between mach time and wall clock (see `docs/DEBUGGING.md`).

## Open items

* **USB port mapping is not done.** `USBToolBox.kext` + `UTBDefault.kext` are present but there is no per-port map for this
  laptop yet; `XhciPortLimit` is off. Map the ports with the USBToolBox tool and replace `UTBDefault.kext`. The
  internal keyboard (`0b05:19b6`) must stay mapped as an internal port.
* Only the Recovery environment has been exercised. Installed-system behaviour (sleep, audio, GPU, battery) is outside the
  scope of what was verified here.

## Provenance

The configuration and the SSDTs started from <https://github.com/nullmoth/1401> and were adapted. That repository does not
declare a license; the files here keep their origin visible (OEM table id `ZPSS`, patch comments by their authors).
If you are the author and want them removed or attributed differently, open an issue.
