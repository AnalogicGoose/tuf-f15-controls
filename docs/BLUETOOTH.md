# Bluetooth (MT7925, USB 0489:e11e)

The Bluetooth half of the MT7925 is a USB device (`Wireless_Device`, class ef, interfaces e0/01/01: ACL, SCO, ISO). It is a
standard HCI controller, but only after its RAM code has been downloaded; macOS has no loader for MediaTek parts, so
`bluetoothd` finds no controller (System Information shows a placeholder "BCM_4350C2, Address NULL, State Off").

## Pieces

| Piece | What it does |
|---|---|
| `kexts/MtkBtFw` | Matches interface 0 of 0489:e11e, opens it, downloads `fw/BT_RAM_CODE_MT7925_1_1_hdr.bin` (embedded) and turns the Bluetooth function on, then closes the interface again. The procedure is Linux `drivers/bluetooth/btmtk.c` (`btmtk_usb_setup`, `btmtk_setup_firmware_79xx`, `btmtk_usb_hci_wmt_sync`): WMT commands as HCI vendor command 0xfc6f on the control endpoint, WMT events polled with a vendor IN request (bRequest 1, wValue 0x30), sections in 250-byte blocks, endpoint reset option 0x74011890 = 0x00010001, WMT FUNC_CTRL 1. Sections the chip reports as already loaded (warm reboot from Windows/Linux) are skipped. |
| `BlueToolFixup.kext` 2.7.2 | From [acidanthera/BrcmPatchRAM](https://github.com/acidanthera/BrcmPatchRAM/releases) (needs Lilu). Required on macOS 12+ so `bluetoothd` accepts a non-Apple controller. Not in this repository; copy it from the release zip. |
| NVRAM `7C436110-...:bluetoothExternalDongleFailed` = `00`, `bluetoothInternalControllerInfo` = 14 x `00` | Also listed in NVRAM/Delete so OpenCore writes them every boot (BrcmPatchRAM README: needed for non-Broadcom controllers). |

## Checking it

```
ioreg -rc MtkBtFw -l | grep -E '"bt-state"|"bt-log"'      # loading / ready / error, and every step
system_profiler SPBluetoothDataType | head -12               # a real address and chipset instead of the placeholder
```

The kexts' `IOLog` output does not reach `dmesg` or the unified log on this machine, so `bt-log` is the record. If two
controllers end up with the same address, BrcmPatchRAM documents the boot-arg `-btlfxallowanyaddr`.

## Undo

Set `Kernel/Add/[BundlePath=MtkBtFw.kext]/Enabled` and `.../BlueToolFixup.kext/Enabled` to false (or restore
`EFI/OC/config.plist.bt-<time>` on the ESP).
