# Third-party material

Everything under `kexts/`, `scripts/`, `test/`, `docs/` is licensed under the [Clear BSD License](LICENSE).
The items below are not covered by that license, or carry attribution requirements of their own.

| Item | Where | Origin | License |
|---|---|---|---|
| MT7925 hardware layer (register map, WFDMA ring setup, MCU/UNI command framing, firmware download sequence, TXD/RXD layouts, STA_REC/BSS_INFO TLVs) | `kexts/itlmt7925/itlmt7925_m1.cpp`, `_m2.cpp`, `_m3.cpp`, `mt7925_regs.h` | Re-implemented from the Linux `mt76` driver (`drivers/net/wireless/mediatek/mt76/mt7925`, `mt76_connac_mcu.c`, `mt792x_*.c`), Copyright (C) 2020, 2023 MediaTek Inc. | BSD-3-Clause-Clear (SPDX header of each upstream file) |
| MT7925 firmware (`WIFI_MT7925_PATCH_MCU_1_1_hdr.bin`, `WIFI_RAM_CODE_MT7925_1_1.bin`) | `fw/`, embedded into `itlmt7925.kext` | `linux-firmware`, `mediatek/mt7925/` (see `fw/README.md` for version and SHA-256) | MediaTek firmware license, `fw/LICENCE.mediatek`. Redistribution is permitted for use with devices containing MediaTek chipsets. **These files are not under the Clear BSD License.** |
| OpenCore patch (`patches/opencore/fix-zen5-fsb.diff`) | applied to `OcCpuLib` at build time | Patch against acidanthera/OpenCorePkg | OpenCorePkg is BSD-3-Clause; the patch is offered under the same terms as this repository |
| Base OpenCore configuration (`efi/OC/config.plist`, `efi/OC/ACPI/*.aml`) | `efi/` | Derived from the configuration published at <https://github.com/nullmoth/1401>, adapted to this laptop | No license is declared by that repository. See `efi/README.md` |
| Third-party kexts used by the EFI | **not included** | Listed with versions and upstream projects in `efi/KEXTS.md` | Each under its own license; download them from upstream |

## Design references (no code copied)

* [OpenIntelWireless/itlwm](https://github.com/OpenIntelWireless/itlwm) (GPL-2.0): the idea of a kext that registers as a plain
  Ethernet-class network device and does its own 802.11 handling instead of going through `IO80211Family`. No itlwm code is
  included in this repository.
* [acidanthera/MacKernelSDK](https://github.com/acidanthera/MacKernelSDK): kernel headers and libraries used by `scripts/build.sh`
  (downloaded at build time, not redistributed).
* OpenCore documentation, Dortania guides, and the XNU / IOKit sources published by Apple (IOPCIFamily) were read while
  tracking down the USB problem described in `docs/USB-XHCI-LAG.md`.
