// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// Host test for kexts/itlmt7925/itl_caps.cpp. Build: c++ -std=c++17 -I../kexts/itlmt7925 caps_test.cpp ../kexts/itlmt7925/itl_caps.cpp -o caps_test
#include "itl_caps.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

static void put(std::vector<uint8_t> &v, uint8_t id, std::initializer_list<uint8_t> body)
{
    v.push_back(id);
    v.push_back((uint8_t)body.size());
    v.insert(v.end(), body.begin(), body.end());
}

// Elements as a typical 2x2 11ac AP sends them (channel 149, 80 MHz block centered on 155).
static std::vector<uint8_t> ap80()
{
    std::vector<uint8_t> v;
    put(v, 0, {'a', 'p'});                                                  // SSID: must be skipped
    std::vector<uint8_t> ht(26, 0);
    ht[0] = 0xef; ht[1] = 0x09;                                             // HT cap info 0x09ef: 40 MHz, SGI, LDPC
    ht[2] = 0x17;                                                           // A-MPDU factor 3, density 5
    ht[3] = 0xff; ht[4] = 0xff;                                             // MCS 0-15
    v.push_back(45); v.push_back(26); v.insert(v.end(), ht.begin(), ht.end());
    std::vector<uint8_t> hto(22, 0);
    hto[0] = 149; hto[1] = 0x0d;                                            // primary 149, secondary above, any width
    v.push_back(61); v.push_back(22); v.insert(v.end(), hto.begin(), hto.end());
    put(v, 191, {0x32, 0x01, 0x80, 0x33, 0xfa, 0xff, 0x34, 0x03, 0xfa, 0xff, 0x34, 0x03});   // VHT cap, map 0xfffa, 2 streams, MCS 0-9
    put(v, 192, {1, 155, 0, 0xfc, 0xff});                                   // VHT op: 80 MHz, seg0 155
    return v;
}

int main()
{
    itl_ap_caps c;
    auto v = ap80();
    itl_caps_parse(v.data(), v.size(), &c);
    CHECK(c.ht && c.htOp && c.vht && c.vhtOp, "parse: HT, HT op, VHT, VHT op present");
    CHECK(c.htCap == 0x09ef && c.ampduParm == 0x17 && c.mcsRx[0] == 0xff && c.mcsRx[1] == 0xff && c.mcsRx[2] == 0, "parse: HT fields");
    CHECK(c.htOpPrimary == 149 && c.htOpInfo == 0x0d, "parse: HT operation fields");
    CHECK(c.vhtCap == 0x33800132 && c.vhtRxMcs == 0xfffa && c.vhtTxMcs == 0xfffa, "parse: VHT fields");
    CHECK(c.vhtOpWidth == 1 && c.vhtSeg0 == 155 && c.vhtSeg1 == 0, "parse: VHT operation fields");

    uint8_t bw, center;
    itl_caps_channel(&c, 149, &bw, &center);
    CHECK(bw == 2 && center == 155, "channel: 80 MHz around seg0");
    CHECK(itl_caps_nss(&c) == 2, "nss: VHT map allows two streams");
    CHECK(itl_caps_max_rate(&c, bw) == 866, "max rate: 2 streams, 80 MHz, MCS 9 (2 x 433)");

    // legacy 160 MHz encoding: seg0 is the 160 MHz center (50); primary 36 lies in the lower 80 MHz half -> center 42
    itl_ap_caps w = c;
    w.vhtOpWidth = 2; w.vhtSeg0 = 50;
    itl_caps_channel(&w, 36, &bw, &center);
    CHECK(bw == 2 && center == 42, "channel: legacy 160 MHz encoding falls back to the 80 MHz half holding the primary");
    itl_caps_channel(&w, 60, &bw, &center);
    CHECK(bw == 2 && center == 58, "channel: ... and the upper half");

    // new 160 MHz encoding: width 1, seg1 = 160 center; seg0 stays the 80 MHz block of the primary
    w = c;
    w.vhtSeg0 = 42; w.vhtSeg1 = 50;
    itl_caps_channel(&w, 36, &bw, &center);
    CHECK(bw == 2 && center == 42, "channel: new 160 MHz encoding uses seg0");

    // HT-only AP, secondary above / below
    std::vector<uint8_t> h;
    std::vector<uint8_t> ht(26, 0);
    ht[0] = 0xef; ht[1] = 0x09; ht[3] = 0xff; ht[4] = 0xff;
    h.push_back(45); h.push_back(26); h.insert(h.end(), ht.begin(), ht.end());
    std::vector<uint8_t> hto(22, 0);
    hto[0] = 36; hto[1] = 0x05;                                             // secondary above, any width
    h.push_back(61); h.push_back(22); h.insert(h.end(), hto.begin(), hto.end());
    itl_caps_parse(h.data(), h.size(), &c);
    itl_caps_channel(&c, 36, &bw, &center);
    CHECK(!c.vht && bw == 1 && center == 38, "channel: HT 40 MHz, secondary above");
    CHECK(itl_caps_max_rate(&c, bw) == 300, "max rate: HT 40 MHz two streams SGI");
    h[2 + 26 + 2 + 1] = 0x07;                                               // secondary below
    itl_caps_parse(h.data(), h.size(), &c);
    itl_caps_channel(&c, 40, &bw, &center);
    CHECK(bw == 1 && center == 38, "channel: HT 40 MHz, secondary below");
    h[2 + 26 + 2 + 1] = 0x01;                                               // 20 MHz BSS (any-width bit clear)
    itl_caps_parse(h.data(), h.size(), &c);
    itl_caps_channel(&c, 36, &bw, &center);
    CHECK(bw == 0 && center == 36, "channel: HT 20 MHz BSS stays 20 MHz");

    // legacy AP and truncated input must not crash or invent capabilities
    std::vector<uint8_t> l;
    put(l, 1, {0x8c, 0x12, 0x98, 0x24});
    itl_caps_parse(l.data(), l.size(), &c);
    itl_caps_channel(&c, 44, &bw, &center);
    CHECK(!c.ht && !c.vht && bw == 0 && center == 44 && itl_caps_nss(&c) == 1 && itl_caps_max_rate(&c, bw) == 54, "legacy AP: nothing advertised");
    v = ap80();
    itl_caps_parse(v.data(), v.size() - 3, &c);                              // last element cut short
    CHECK(c.ht && c.vht && !c.vhtOp, "truncated VHT operation is ignored");

    // one-stream AP
    v = ap80();
    itl_caps_parse(v.data(), v.size(), &c);
    c.vhtRxMcs = 0xfffe;                                                     // NSS1 MCS 0-9, NSS2 not supported
    CHECK(itl_caps_nss(&c) == 1 && itl_caps_max_rate(&c, 2) == 433, "nss: one-stream AP");

    // what we advertise
    uint8_t e[40];
    CHECK(itl_caps_build_ht(e) == 28 && e[0] == 45 && e[1] == 26, "build: HT element header");
    CHECK((e[2] | e[3] << 8) == 0x016f && e[4] == 0x17 && e[5] == 0xff && e[6] == 0xff && e[7] == 0, "build: HT capability info, A-MPDU, MCS 0-15");
    bool zero = true;
    for (int i = 7; i < 28; i++) zero = zero && e[i] == 0;
    CHECK(zero, "build: HT remaining fields zero (no TxBF, no ASEL, no PCO)");
    CHECK(itl_caps_build_vht(e) == 14 && e[0] == 191 && e[1] == 12, "build: VHT element header");
    uint32_t cap = e[2] | e[3] << 8 | e[4] << 16 | (uint32_t)e[5] << 24;
    CHECK(cap == 0x03800130, "build: VHT capability info (3895, 80 MHz, LDPC, SGI80, RX STBC 1, A-MPDU 2^20)");
    CHECK((e[6] | e[7] << 8) == 0xfffa && (e[10] | e[11] << 8) == 0xfffa && (e[8] | e[9] << 8) == 867, "build: VHT MCS map and highest rate");

    // what we build is what we parse back
    std::vector<uint8_t> self;
    uint8_t buf[40];
    self.insert(self.end(), buf, buf + itl_caps_build_ht(buf));
    self.insert(self.end(), buf, buf + itl_caps_build_vht(buf));
    itl_caps_parse(self.data(), self.size(), &c);
    CHECK(c.ht && c.vht && c.vhtRxMcs == 0xfffa && itl_caps_nss(&c) == 2, "round trip: parse(build())");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
