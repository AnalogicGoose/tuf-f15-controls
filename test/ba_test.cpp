// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// Host test for kexts/itlmt7925/itl_ba.cpp. Build: c++ -std=c++17 -I../kexts/itlmt7925 ba_test.cpp ../kexts/itlmt7925/itl_ba.cpp -o ba_test
#include "itl_ba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <algorithm>

static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

struct Sink {
    std::vector<uint32_t> got;            // payload id of every delivered frame
    void clear() { got.clear(); }
};
static void deliver(void *ctx, const uint8_t *f, uint32_t len)
{
    Sink *s = (Sink *)ctx;
    uint32_t id;
    if (len >= 4) memcpy(&id, f, 4); else id = 0xffffffffu;
    s->got.push_back(id);
}

struct Rx {
    itl_reorder r;
    itl_ba_slot *mem;
    Sink s;
    explicit Rx(uint16_t ssn, uint16_t size = 64) { mem = (itl_ba_slot *)calloc(size, sizeof(itl_ba_slot)); itl_reorder_start(&r, mem, ssn, size); }
    ~Rx() { free(mem); }
    void put(uint32_t id, uint32_t now = 0)
    {
        uint8_t f[64];
        memset(f, 0, sizeof(f));
        memcpy(f, &id, 4);
        itl_reorder_rx(&r, (uint16_t)(id & 0xfff), f, sizeof(f), now, deliver, &s);
    }
};

static bool inOrder(const std::vector<uint32_t> &v)
{
    for (size_t i = 1; i < v.size(); i++) if (v[i] <= v[i - 1]) return false;
    return true;
}

int main()
{
    // ---- sequence number arithmetic
    CHECK(itl_sn_less(4095, 1) && !itl_sn_less(1, 4095) && !itl_sn_less(7, 7) && itl_sn_inc(4095) == 0 && itl_sn_sub(2, 4) == 4094,
          "sn: wrap-around arithmetic");
    CHECK(itl_reorder_size(0) == 64 && itl_reorder_size(256) == 64 && itl_reorder_size(64) == 64 && itl_reorder_size(40) == 32 &&
          itl_reorder_size(1) == 1 && itl_reorder_size(7) == 4,
          "size: largest power of two not above the request, 64 at most");

    // ---- in order
    {
        Rx x(100);
        for (uint32_t i = 100; i < 110; i++) x.put(i);
        CHECK(x.s.got.size() == 10 && inOrder(x.s.got) && x.r.nDirect == 10 && x.r.nBuffered == 0, "reorder: in-order frames pass straight through");
    }
    // ---- a hole is filled
    {
        Rx x(0);
        x.put(0); x.put(2); x.put(3); x.put(1);
        CHECK(x.s.got == std::vector<uint32_t>({0, 1, 2, 3}) && x.r.nframes == 0, "reorder: late frame releases the frames queued behind it");
    }
    // ---- duplicates and old frames
    {
        Rx x(10);
        x.put(10); x.put(12); x.put(12); x.put(9); x.put(10);
        CHECK(x.s.got == std::vector<uint32_t>({10}) && x.r.nDup == 1 && x.r.nOld == 2, "reorder: duplicate buffered frame and frames behind the window are dropped");
    }
    // ---- window slides when a frame lands beyond it (lost frame 1)
    {
        Rx x(0);
        x.put(0);
        for (uint32_t i = 2; i <= 65; i++) x.put(i);                    // 2..64 wait behind the hole at 1; 65 is the first beyond head(1)+64
        CHECK(x.r.head == 66 && x.s.got.size() == 65 && inOrder(x.s.got) && x.s.got.front() == 0 && x.s.got[1] == 2,
              "reorder: a frame beyond the window slides it and flushes the hole");
    }
    // ---- timeout flushes a hole
    {
        Rx x(0);
        x.put(0, 0); x.put(2, 10); x.put(4, 20);
        itl_reorder_tick(&x.r, 50, 100, deliver, &x.s);
        CHECK(x.s.got == std::vector<uint32_t>({0}), "tick: nothing released before the timeout");
        itl_reorder_tick(&x.r, 110, 100, deliver, &x.s);
        CHECK(x.s.got == std::vector<uint32_t>({0, 2}) && x.r.nTimeout == 1, "tick: frame older than the timeout is released, the younger one waits");
        itl_reorder_tick(&x.r, 125, 100, deliver, &x.s);
        CHECK(x.s.got == std::vector<uint32_t>({0, 2, 4}) && x.r.nframes == 0, "tick: next one follows once it is old enough");
    }
    // ---- BAR
    {
        Rx x(0);
        x.put(0); x.put(3); x.put(4);
        itl_reorder_bar(&x.r, 3, deliver, &x.s);
        CHECK(x.s.got == std::vector<uint32_t>({0, 3, 4}) && x.r.head == 5, "bar: window moves to the starting sequence number");
        itl_reorder_bar(&x.r, 2, deliver, &x.s);
        CHECK(x.r.head == 5, "bar: an old starting sequence number is ignored");
    }
    // ---- QoS-Null occupies its sequence number but delivers nothing
    {
        Rx x(0);
        uint8_t f[8] = {0};
        itl_reorder_rx(&x.r, 1, f, 0, 0, deliver, &x.s);                // qos-null with sn 1 arrives first
        x.put(0);
        x.put(2);
        CHECK(x.s.got == std::vector<uint32_t>({0, 2}) && x.r.head == 3, "reorder: QoS-Null keeps its place in the sequence");
    }
    // ---- wrap-around of the 12-bit counter
    {
        Rx x(4090);
        std::vector<uint32_t> order = {4090, 4092, 4091, 4094, 4093, 4095, 0 + 4096, 2 + 4096, 1 + 4096};
        for (uint32_t id : order) x.put(id);
        CHECK(x.s.got == std::vector<uint32_t>({4090, 4091, 4092, 4093, 4094, 4095, 4096, 4097, 4098}), "reorder: sequence numbers wrap 4095 -> 0");
    }
    // ---- stop releases in order
    {
        Rx x(0);
        x.put(2); x.put(1); x.put(5);
        itl_reorder_stop(&x.r, deliver, &x.s);
        CHECK(x.s.got == std::vector<uint32_t>({1, 2, 5}) && !x.r.active, "stop: buffered frames are released in order");
        x.put(7);
        CHECK(x.s.got.back() == 7, "stop: afterwards frames pass through");
    }
    // ---- smaller window
    {
        Rx x(0, 8);
        x.put(0); x.put(2);
        for (uint32_t i = 3; i < 12; i++) x.put(i);                     // 11 is beyond head(1)+8
        CHECK(inOrder(x.s.got) && x.r.head == 12, "reorder: window of 8");
    }
    // ---- randomized: bounded reordering and 3% loss
    {
        srand(12345);
        for (int round = 0; round < 20; round++) {
            Rx x((uint16_t)(rand() & 0xfff));
            const uint32_t base = x.r.head;
            const int N = 6000;
            std::vector<uint32_t> ids;
            for (int i = 0; i < N; i++) if (rand() % 100 >= 3) ids.push_back(base + i);           // 3% never arrive
            // displace each frame by up to 30 positions: sort by position + noise
            std::vector<std::pair<int, uint32_t>> v;
            for (size_t i = 0; i < ids.size(); i++) v.push_back({(int)i * 8 + rand() % 240, ids[i]});
            std::sort(v.begin(), v.end());
            uint32_t now = 0;
            for (auto &p : v) {
                x.put(p.second, now);
                now += 1;
                if (now % 16 == 0) itl_reorder_tick(&x.r, now, 100, deliver, &x.s);
            }
            itl_reorder_stop(&x.r, deliver, &x.s);
            // frame ids are base+i; delivered ids carry the 12-bit sequence in the low bits via (id & 0xfff) only,
            // so compare as unwrapped ids (they are kept whole in the payload)
            bool ok = inOrder(x.s.got) && x.s.got.size() == ids.size();
            if (!ok) printf("round %d: delivered %zu of %zu in order=%d old=%u dup=%u flushed=%u\n", round, x.s.got.size(), ids.size(),
                            (int)inOrder(x.s.got), x.r.nOld, x.r.nDup, x.r.nFlushed);
            if (!ok) { fails++; break; }
            if (round == 19) printf("ok   random: 20 rounds x %d frames, 3%% loss, displacement < 30: every frame delivered once, in order\n", N);
        }
    }

    // ---- action frames
    {
        uint8_t b[16];
        itl_ba_action a;
        // captured-style ADDBA request: token 5, immediate, TID 0, buffer 64, ssn 0x123, timeout 0, A-MSDU supported
        size_t n = itl_addba_req_build(b, 5, 0, 64, 0x123, 0, true);
        CHECK(n == 9 && b[0] == 3 && b[1] == 0 && b[2] == 5 && b[3] == 0x03 && b[4] == 0x10 && b[7] == 0x30 && b[8] == 0x12,
              "addba req: byte layout (params 0x1003, ssc 0x1230)");
        CHECK(itl_ba_action_parse(b, n, &a) && a.action == 0 && a.token == 5 && a.tid == 0 && a.bufsize == 64 && a.ssn == 0x123 &&
                  a.amsdu && a.immediate && a.timeout == 0,
              "addba req: parse(build())");
        n = itl_addba_req_build(b, 9, 5, 32, 4095, 100, false);
        CHECK(itl_ba_action_parse(b, n, &a) && a.tid == 5 && a.bufsize == 32 && a.ssn == 4095 && a.timeout == 100 && !a.amsdu, "addba req: TID 5, ssn 4095");
        n = itl_addba_resp_build(b, 5, ITL_STATUS_SUCCESS, 3, 64, false, 0);
        CHECK(n == 9 && b[1] == 1 && b[3] == 0 && b[4] == 0 && b[5] == 0x0e && b[6] == 0x10, "addba resp: byte layout (params 0x100e)");
        CHECK(itl_ba_action_parse(b, n, &a) && a.action == 1 && a.status == 0 && a.tid == 3 && a.bufsize == 64 && !a.amsdu && a.immediate,
              "addba resp: parse(build())");
        n = itl_addba_resp_build(b, 1, ITL_STATUS_DECLINED, 0, 0, false, 0);
        CHECK(itl_ba_action_parse(b, n, &a) && a.status == 37, "addba resp: declined status");
        n = itl_delba_build(b, 6, true, ITL_REASON_QSTA_LEAVE);
        CHECK(n == 6 && b[2] == 0x00 && b[3] == 0x68 && itl_ba_action_parse(b, n, &a) && a.action == 2 && a.tid == 6 && a.initiator && a.reason == 36,
              "delba: layout and parse(build())");
        CHECK(!itl_ba_action_parse(b, 3, &a), "parse: truncated frame rejected");
        b[0] = 4;
        CHECK(!itl_ba_action_parse(b, 6, &a), "parse: other categories rejected");
        uint8_t bad[9] = {3, 0, 1, 0x3c, 0x00, 0, 0, 0, 0};             // TID 15
        CHECK(!itl_ba_action_parse(bad, sizeof(bad), &a), "parse: invalid TID rejected");
    }
    // ---- BAR
    {
        const uint8_t ra[6] = {2, 0, 0, 0, 0, 1}, ta[6] = {2, 0, 0, 0, 0, 2};
        uint8_t f[20] = {0x84, 0x00, 0, 0};
        memcpy(f + 4, ra, 6); memcpy(f + 10, ta, 6);
        f[16] = 0x04; f[17] = 0x30;                                      // compressed, TID 3
        f[18] = 0x50; f[19] = 0x07;                                      // ssc 0x0750 -> ssn 0x75
        uint8_t tid; uint16_t ssn;
        CHECK(itl_bar_parse(f, sizeof(f), ra, ta, &tid, &ssn) && tid == 3 && ssn == 0x75, "bar: parse TID and starting sequence number");
        CHECK(!itl_bar_parse(f, sizeof(f), ta, ra, &tid, &ssn), "bar: wrong addresses rejected");
        f[16] = 0x06;
        CHECK(!itl_bar_parse(f, sizeof(f), ra, ta, &tid, &ssn), "bar: multi-TID variant rejected");
    }

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
