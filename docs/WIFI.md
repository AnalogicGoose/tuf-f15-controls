# itlmt7925: MediaTek MT7925 Wi-Fi on macOS

`itlmt7925.kext` drives the MediaTek MT7925 (PCI `14c3:7925`, a Wi-Fi 7 chip) without any Apple Wi-Fi framework. It brings its own minimal 802.11 client and presents the result to macOS as a wired Ethernet
interface (`itlmt7925Eth`), the same trick the itlwm project uses for Intel cards.

## Status

Verified on the reference machine (GA403WM, macOS 15.6.1 Recovery booted from a USB stick):

* firmware download and start (patch + RAM image), NIC capabilities read, MAC address read from the firmware
* active scan of 2.4 GHz (1, 6, 11) and 5 GHz (36, 40, 44, 48, 149, 153, 157, 161, 165) with directed probes
* open-system authentication, association, WPA2-PSK 4-way handshake (CCMP), pairwise and group key install
* DHCP exchange, ARP, DNS queries with answers and an NTP request observed on the interface
* up to four configured networks, picked by priority and availability

Not done, in rough order of importance:

| Gap | Consequence |
|---|---|
| fixed transmit rate: legacy OFDM 6 Mb/s, no HT/VHT/HE, no aggregation, no QoS | low throughput. No benchmark has been run; do not expect more than a few Mb/s |
| no Wi-Fi UI, no runtime network switching (no IO80211) | shows up as Ethernet; networks are chosen from the kext plist |
| no reconnect after the AP drops the link, no re-key, no power save | after a drop reboot or reload; battery use is not optimised |
| WPA2-PSK/CCMP only | no open networks, no WPA3/SAE, no enterprise, no 6 GHz scan |
| regulatory domain is a fixed world domain (`00`: 2.4 GHz ch 1-11, 5 GHz ch 36-165); MAC address is the one stored in the card | |
| only the Recovery environment was exercised | behaviour of a full install (sleep, wake, power) is unknown |

## Speed: what to expect, and the ceiling

**Today (measured: nothing; estimated from the fixed rate).** The data path transmits at legacy OFDM 6 Mb/s, which is
0.75 MB/s on paper and roughly 0.5 MB/s once framing and ACK overhead are paid. A 16 GB download (a full macOS installer)
therefore takes about 6 hours at the theoretical rate and about 9 hours in practice, and the driver cannot reconnect after a
drop, so a single transfer of that size is unlikely to finish. Use USB Ethernet or USB tethering, or copy the installer from
another machine, until the rate work below is done.

**Hardware ceiling (chip, not driver).** The MT7925 is a 2x2 Wi-Fi 7 (802.11be) chip:

| Band / width | PHY rate (2 spatial streams) | Typical real throughput |
|---|---|---|
| 2.4 GHz, 40 MHz, HE | about 573 Mb/s | 150-300 Mb/s |
| 5 GHz, 80 MHz, VHT | 867 Mb/s | 400-600 Mb/s |
| 5 / 6 GHz, 160 MHz, HE | about 2.4 Gb/s | around 1 Gb/s or more, router dependent |
| 6 GHz, 320 MHz, EHT | about 2.9 Gb/s | needs a Wi-Fi 7 router; not planned here |

PHY rates are the vendor/standard figures; real throughput numbers are typical values for chips of this class, not
measurements from this driver.

**What stands between 6 Mb/s and those numbers**, in order of payoff (all are in the "Not done" table above):

1. Advertise HT/VHT/HE capabilities in the STA record and stop forcing the legacy rate. Firmware rate control then picks MCS.
2. A-MPDU: BlockAck session setup (ADDBA) on TX and a reorder buffer on RX.
3. QoS data frames and WMM queues, then 40/80 MHz channel width negotiation.
4. Interrupt-driven DMA instead of the polling thread, so the CPU is not the limit at hundreds of Mb/s.

**How to measure** once a link is up (none of this has been done): run `iperf3 -s` on another machine on the same LAN and
`iperf3 -c <ip> -t 30` (and `-R` for the reverse direction) from the Hackintosh; compare against the counters in the `itls0`
line (`rxd`, `tx`, `fail`). Record results in this file with the AP model, band, channel and distance.

## HT/VHT mode (`itlmt_fast=1`)

Off by default, so the verified legacy path is untouched. With `itlmt_m3=1 itlmt_fast=1`, and only when the chosen AP is on
5 GHz and advertises HT, the driver:

1. parses the AP's HT / VHT Capabilities and Operation elements from its beacon (`itl_caps.cpp`, covered by `test/caps_test.cpp`);
2. adds HT Capabilities (2 streams, 20/40 MHz, short GI, LDPC), VHT Capabilities (2 streams, 80 MHz, MCS 0-9) and a WMM element to the association request;
3. tells the firmware the AP's capabilities: `STA_REC_HT`, `STA_REC_VHT`, HT/VHT bits and A-MPDU parameters in `STA_REC_PHY`, the HT MCS bitmask in `STA_REC_RA`, bandwidth and streams in `STA_REC_STATE`;
4. runs the BSS at the AP's width (20/40/80 MHz, never wider) with `PHY_MODE_A|AN|AC` and 2 streams;
5. sends unicast data as QoS data **without** the fixed-rate flag, so the firmware's rate control picks the MCS. Handshake (EAPOL),
   management and group-addressed frames still use the fixed 6 Mb/s basic rate;
6. reports the estimated best PHY rate as the Ethernet link speed instead of 54 Mb/s.

All the firmware message layouts come from the Linux `mt7925` driver (`mcu.c`, `mac.c`, `mt76_connac_mcu.h`). The log line
`fast: on ht=1 vht=1 bw=2 center=155 nss=2 rate~866 ...` in `m3-log` shows what was negotiated; `fast: off ...` means the legacy path was used.

Status: it compiles (syntax-checked against MacKernelSDK) and the capability parsing is unit-tested, but **it has not been run on the
GA403WM**. If the association or the handshake fails with `itlmt_fast=1`, remove the boot-arg to get back the known-good behaviour and
read the log (`docs/DEBUGGING.md`).

What this mode does not do yet, and what to expect: no BlockAck (ADDBA) and therefore no A-MPDU in either direction, no A-MSDU, no
HE/EHT, no 2.4 GHz, widths above 80 MHz fall back to the 80 MHz block holding the primary channel. Without aggregation every frame pays its
own channel access and ACK, so expect a fraction of the PHY rate (my estimate: low hundreds of Mb/s on a good 5 GHz link, still far above 6 Mb/s).

## How it works

The driver is split by milestone, each file a port of the matching part of the Linux
[`mt76`](https://github.com/torvalds/linux/tree/master/drivers/net/wireless/mediatek/mt76) driver
(BSD-3-Clause-Clear, see `NOTICE.md`):

| File | Stage | Linux counterpart |
|---|---|---|
| `itlmt7925.cpp` | probe: map BAR0, take driver ownership from the firmware, read chip id | `mt7925/pci.c`, `mt792xe_mcu_drv_pmctrl` |
| `itlmt7925_m1.cpp` | WFDMA rings (polled), MCU command transport, patch and RAM firmware download | `mt792x_dma.c`, `mt76_connac_mcu.c`, `mt792x_core.c` |
| `itlmt7925_m2.cpp` | UNI command transport, device/BSS setup, scan, RXD parsing | `mt7925/mcu.c`, `mt7925/mac.c` |
| `itlmt7925_m3.cpp` | connect: ROC, auth/assoc frames, STA/BSS state, EAPOL, keys, data path | `mt7925/main.c`, `mcu.c`, `mac.c` |
| `itlmt7925_eth.cpp` | `IOEthernetController` + interface, frame conversion hooks | n/a |
| `itl_wpa.cpp` | PBKDF2-SHA1, PRF-SHA1, HMAC-SHA1, AES key unwrap, EAPOL MIC (host-testable) | n/a |
| `itl_caps.cpp` | HT/VHT element parsing, channel width choice, our advertised HT/VHT elements (host-testable) | `mac80211` capability handling |
| `fw_blob.c` | embeds `fw/*.bin` with `.incbin` | n/a |

The driver runs on its own thread, polled (it does not use interrupts), started after the delay given by `itlmt_m1`.
It has no dependency on IO80211Family; `IONetworkingFamily` provides the Ethernet interface.

Connection sequence (`m3Run`): hardware prep (domain info, firmware log, eeprom=efuse, RTS threshold), wildcard scan,
then for each configured SSID in priority order (networks already seen in the wildcard scan first) up to three directed
scans until the AP is found, PMK from the passphrase, BSS + STA record, remain-on-channel, authentication, association,
BSS enable and STA state 2, 4-way handshake, PTK/GTK install, interface creation.

## Configuration

### Boot-args

| Boot-arg | Effect |
|---|---|
| `itlmt_m1=<seconds>` | **required** to do anything beyond probing: wait that long, then download firmware. The reference machine uses `75` |
| `itlmt_m2=1` | after the firmware is running: bring up the interface and scan |
| `itlmt_m3=1` | also connect (implies `itlmt_m2=1`) |
| `itlmt_fast=1` | **experimental, untested on hardware**: use 802.11n/ac when the AP is on 5 GHz (see [HT/VHT mode](#htvht-mode-itlmt_fast1)) |
| `itlmt_hold=<seconds>` | repeat the probe log on the verbose console for that long (to photograph it) |

Without any of them the kext only maps the card, takes ownership and logs the chip id (`CHIPID=...7925`).

### Networks

Credentials are personality keys of the kext (`Info.plist`, `IOKitPersonalities > itlmt7925`):

| Key | Value |
|---|---|
| `itl_ssid`, `itl_psk` | network 1: SSID (max 32 bytes), WPA2 passphrase (8..63 characters) |
| `itl_ssid2`/`itl_psk2` ... `itl_ssid4`/`itl_psk4` | networks 2..4; list order is priority; empty = unused |

The repository ships these keys **empty**. Fill them locally and never commit the result:

```powershell
# Windows, elevated shell (reading Wi-Fi keys needs it). Copies the passphrases of saved profiles into the built kext.
.\scripts\inject-creds.ps1 -Kext Y:\EFI\OC\Kexts\itlmt7925.kext -Ssid 'HomeWifi','Cafe'
```

The script prints only name and passphrase lengths. The passphrase ends up in plain text inside the kext on your boot
medium: treat that medium like a password file. On macOS, edit the plist by hand or with `plutil -replace`.

A matching network is chosen automatically; there is no way to switch while running.

### Build

```bash
bash scripts/build.sh            # needs Xcode command line tools, builds build/itlmt7925.kext (+ the other kexts)
bash scripts/build.sh itlmt7925  # only this one
```

`scripts/build.sh` clones `acidanthera/MacKernelSDK` on first use. Without a Mac, the `build-kexts` GitHub workflow
builds the same on a macOS runner and uploads the kexts as an artifact. The host test (`test/`) checks the WPA2
primitives against Python reference vectors and runs in the same workflow.

## Reading the result

The driver logs to the verbose console, to properties on its registry entry (`m1-log`, `m2-log`, `m3-log`, `m3-res`) and to
firmware NVRAM, which survives a hang or panic and can be read from Windows or Linux. See `docs/DEBUGGING.md`. A successful run
ends with `RESULT rc=0 link up;ethernet interface created` in `itlk0..itlk7` and a line like
`up=145s rxd=5905 eapol=2 eth=5903 drop=0 tx=4698 fail=0` in `itls0`.

## Hardware notes learned on the way

* The EFI of this repository ships `SSDT-Disable_Network_GPP5.aml`, which spoofs the ids of `\_SB.PCI0.GPP5.WLAN` so that
  macOS ignores the card. It has to be **disabled** for this driver, otherwise `IOPCIMatch 0x792514c3` never matches.
* The channel domain (`UNI_SET_DOMAIN_INFO`) has to be sent before the first scan; Linux does it once at start-up.
* The firmware's default channel dwell time (~35 ms) misses most beacons; the scan request sets 60 ms minimum / 120 ms.
