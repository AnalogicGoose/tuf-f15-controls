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

- [ ] Rate control: HT/VHT/HE capabilities in the STA record, fixed 6 Mb/s transmit rate removed, A-MPDU.
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
