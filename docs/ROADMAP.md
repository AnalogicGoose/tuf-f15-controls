# Roadmap

Open items, roughly by value. Nothing here is promised; PRs welcome.

## EFI / system

- [ ] **USB port mapping** for the GA403WM (USBToolBox map replacing the `UTBDefault.kext` placeholder, keeping the internal
      keyboard `0b05:19b6`). Not started.
- [ ] Verify a full macOS installation on the internal disk (everything so far ran from the Recovery image).
- [ ] Remove the no-op `IOPCIMSIMode` entries and the debug boot-args from the reference configuration and test the result.
- [ ] Build the USB map and the clean configuration into a tagged EFI release.

## `ItlXhciKick`

- [ ] Measure the CPU/power cost of 4 × 1 kHz kicks and adapt the period automatically (e.g. kick only controllers that
      have HID devices, back off when ring 1 is idle).
- [ ] Find out *why* interrupter 1 never raises the single vector (controller or macOS programming); if it is an enable bit,
      set it instead of polling.
- [ ] Try it on other AMD generations (Phoenix, Hawk Point, Strix Halo) and report.

## `itlmt7925`

Speed targets (estimates, see [WIFI.md](WIFI.md#speed-what-to-expect-and-the-ceiling)): today about 0.5 MB/s (a 16 GB
download takes about 9 h); with HT/VHT on 5 GHz 80 MHz a few hundred Mb/s; the chip itself is a 2x2 Wi-Fi 7 part with a PHY
ceiling of about 2.4 Gb/s at 160 MHz. No throughput benchmark exists yet.

- [ ] **Benchmark first**: `iperf3` TX/RX on the current driver, recorded in `docs/WIFI.md`, so every change below has a baseline.
- [ ] Rate control, step 1 (`itlmt_fast=1`): HT/VHT capabilities in the association and the STA record, firmware rate control for unicast
      data, 20/40/80 MHz BSS. **Implemented, compiles, host-tested parsing; not yet run on hardware**: test it and record the result.
- [ ] Rate control, step 2: BlockAck (ADDBA/DELBA) with `STA_REC_BA` for RX and TX A-MPDU, then A-MSDU. This is where most of the speed beyond
      "low hundreds of Mb/s" comes from, and RX aggregation matters most for downloads.
- [ ] Rate control, step 3: HE (`STA_REC_HE_V2`), 2.4 GHz HT, 160 MHz.
- [ ] TX path: do not wait for the descriptor to be consumed per packet (`m3Tx`), track TX-free events before reusing a staging slot.
- [ ] Reconnect when the AP drops the link; group key re-key; periodic scan for a better configured network.
- [ ] Power save (no power-save negotiation is implemented).
- [ ] WPA3-SAE, open networks, 6 GHz scan, and a regulatory domain chosen from configuration (it is a fixed world domain `00` now).
- [ ] Move from a polled worker to interrupts (MSI) so that the kext is not a busy thread.
- [ ] Use the real macOS Wi-Fi stack: `IO80211Family` / AirportItlwm-style integration for the menu bar and `airport` tools.
- [ ] Bluetooth: the MT7925 has a separate USB Bluetooth function, out of scope so far.
- [ ] Option to load the firmware from a file instead of embedding it, when a filesystem is available.

## Project

- [ ] First tagged release with CI-built kexts.
- [ ] Contribution guide and issue templates.
