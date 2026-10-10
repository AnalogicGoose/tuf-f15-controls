// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// itl_caps.h - 802.11n/ac capability handling with no OS dependencies (compiles in the kext and in the host test,
// test/caps_test.cpp): parse the HT / VHT elements of the AP's beacon, pick the operating channel width, and build the HT / VHT
// capability elements for our association request.
//
// What the station advertises is what the driver can actually do today: 2 spatial streams, 20/40/80 MHz, short GI, LDPC,
// no beamforming, no STBC TX, no A-MSDU (the RX buffers are 2 KiB). HE (802.11ax) and EHT (802.11be) are not handled yet.
#ifndef ITL_CAPS_H
#define ITL_CAPS_H

#include <stdint.h>
#include <stddef.h>

#define ITL_CAPS_MAX_NSS 2

// Capabilities of the AP, parsed from the IEs of its beacon / probe response (the frame body after the fixed fields).
struct itl_ap_caps {
    bool ht;                  // HT Capabilities element (id 45) present
    uint16_t htCap;           // HT Capabilities Info
    uint8_t ampduParm;        // A-MPDU parameters: bits 0-1 max length exponent, bits 2-4 min MPDU start spacing
    uint8_t mcsRx[10];        // supported MCS set, RX MCS bitmask (bytes 0-9)
    bool htOp;                // HT Operation element (id 61) present
    uint8_t htOpPrimary;      // primary channel from HT Operation
    uint8_t htOpInfo;         // HT Operation Information byte 0: bits 0-1 secondary channel offset, bit 2 any width
    bool vht;                 // VHT Capabilities element (id 191) present
    uint32_t vhtCap;          // VHT Capabilities Info
    uint16_t vhtRxMcs;        // VHT-MCS map, what the AP can receive (2 bits per NSS, 3 = not supported)
    uint16_t vhtTxMcs;        // VHT-MCS map, what the AP transmits
    bool vhtOp;               // VHT Operation element (id 192) present
    uint8_t vhtOpWidth;       // 0: 20/40, 1: 80/160/80+80, 2: 160 (legacy encoding), 3: 80+80 (legacy encoding)
    uint8_t vhtSeg0, vhtSeg1; // channel center frequency segment 0 / 1 (channel numbers)
};

void itl_caps_parse(const uint8_t *ies, size_t len, itl_ap_caps *c);

// Channel the BSS should be configured for on the 5 GHz band, never wider than 80 MHz (what we advertise).
//   bw: 0 = 20 MHz, 1 = 40 MHz, 2 = 80 MHz (the firmware's CMD_CBW_* values); center: center channel number.
void itl_caps_channel(const itl_ap_caps *c, uint8_t primary, uint8_t *bw, uint8_t *center);

// Spatial streams usable towards this AP: min(ITL_CAPS_MAX_NSS, what the AP can receive); at least 1.
uint8_t itl_caps_nss(const itl_ap_caps *c);

// Our HT Capabilities element (28 bytes, id and length included) and VHT Capabilities element (14 bytes). Returns the size.
size_t itl_caps_build_ht(uint8_t *out);
size_t itl_caps_build_vht(uint8_t *out);

// PHY rate in Mb/s of the best rate we could use to this AP (for the link speed shown by macOS). Estimate from the
// negotiated width / streams / guard interval, not a measurement.
uint32_t itl_caps_max_rate(const itl_ap_caps *c, uint8_t bw);

#endif
