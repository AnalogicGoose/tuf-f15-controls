# itlmt7925

Experimental macOS kext for the MediaTek MT7925 (Wi-Fi 7, PCI 14c3:7925) on a hackintosh
(ASUS ROG Zephyrus G14 GA403WM, Ryzen AI 9 HX 370, macOS 15).

Design: itlwm-style. The driver registers as an Ethernet-class service and brings its own
802.11 stack; it does not touch IO80211. Hardware layer is ported from the Linux mt76 driver
(`ref/linux-mt76`, BSD-3-Clause-Clear). Generic net80211/kext skeleton comes from
OpenIntelWireless/itlwm (`ref/itlwm`). Firmware is `ref/firmware` (linux-firmware, redistributable;
see `ref/firmware/WHENCE.txt`).

## Milestones
- M0 Probe: match PCI, map BAR0, take driver ownership, log chip id. **(this commit, untested)**
- M1 Firmware: DMA rings + MCU, load patch and RAM code.
- M2 Scan.
- M3 WPA2-PSK connect + DHCP.

## Status
Nothing has been run on real hardware yet. The kext has not been compiled either.
See `docs/TESTING.md`.
