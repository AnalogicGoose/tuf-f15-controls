# Testing M0

Prerequisite: macOS must be installed and booting on the machine (network via Android tethering
or a USB Ethernet dongle for the install).

## 1. EFI changes
The Wi-Fi card is currently hidden from macOS on purpose. `EFI/OC/ACPI/SSDT-Disable_Network_GPP5.aml`
runs under Darwin and sets `vendor-id`/`device-id` = 0xFFFF, `class-code` = 0xFFFFFFFF and `IOName` = `#display`
on `\_SB.PCI0.GPP5.WLAN`. IOPCIMatch cannot match 14c3:7925 while that is active.

For M0 testing:
- In `config.plist` > `ACPI > Add`, set `SSDT-Disable_Network_GPP5.aml` to `Enabled = false`.
- Add `itlmt7925.kext` to `Kernel > Add` (Arch Any, ExecutablePath `Contents/MacOS/itlmt7925`, PlistPath `Contents/Info.plist`).
- Keep a copy of the working EFI. Test from the USB stick EFI first, not the internal disk.

## 2. Read the result
In macOS Terminal:
    log show --last 5m --predicate 'eventMessage CONTAINS "itlmt7925"'
or  sudo dmesg | grep itlmt7925

Expect:
    itlmt7925: probe 14c3:7925 ...
    itlmt7925: BAR0 mapped, size 0x200000 (or larger)
    itlmt7925: driver own OK
    itlmt7925: CHIPID=0x....7925 ... ASIC revision: 7925xx

If CHIPID reads 0xffffffff the device is not responding (D3 power state, ASPM L1, or BAR/MMIO problem).
