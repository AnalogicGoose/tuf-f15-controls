#include <initializer_list>
#include <cassert>
#include <cstring>
#include "../kexts/TufWmi/tuf_policy.h"
static bool req(uint64_t d, uint64_t v, bool ac = true, bool fan = false, uint64_t v2 = 0) { return tuf_valid_request(d, v, v2, ac, fan); }
int main() {
    // authorization
    assert(tuf_authorized(true, true, false, false));
    assert(tuf_authorized(true, false, true, true));
    assert(!tuf_authorized(true, false, true, false));
    assert(!tuf_authorized(true, false, false, true));
    assert(!tuf_authorized(true, false, false, false));
    assert(!tuf_authorized(false, true, true, true));   // foreign owningTask
    assert(!tuf_authorized(false, false, true, true));
    for (int v : {0, 3, -1, 12}) assert(!tuf_bridge_enabled(v) && !tuf_writes_enabled(v));
    assert(tuf_bridge_enabled(1) && !tuf_writes_enabled(1));
    assert(tuf_bridge_enabled(2) && tuf_writes_enabled(2));

    // profile / charge (unchanged rules)
    assert(req(TUF_PROFILE, 0, false) && req(TUF_PROFILE, 2, false) && req(TUF_PROFILE, 1, true));
    assert(!req(TUF_PROFILE, 1, false) && !req(TUF_PROFILE, 3) && !req(TUF_PROFILE, UINT64_C(0x100000000)));
    assert(!req(UINT64_C(0x100120075), 0));
    for (auto limit : {60u, 80u, 100u}) assert(req(TUF_CHARGE, limit, false));
    for (auto limit : {0u, 70u, 101u}) assert(!req(TUF_CHARGE, limit));
    assert(!req(TUF_CHARGE, UINT64_C(0x100000050)));
    assert(!req(TUF_PROFILE, 0, true, false, 1));       // a stray third scalar is refused
    assert(!req(TUF_CHARGE, 80, true, false, 1));

    // power limits: in range AND only with a custom fan curve active
    assert(!req(TUF_PL1, 45, true, false));
    assert(req(TUF_PL1, 45, true, true) && req(TUF_PL1, TUF_PL1_MIN, true, true) && req(TUF_PL1, TUF_PL1_MAX, true, true));
    assert(!req(TUF_PL1, TUF_PL1_MIN - 1, true, true) && !req(TUF_PL1, TUF_PL1_MAX + 1, true, true));
    assert(!req(TUF_PL1, UINT64_C(0x10000002D), true, true));
    assert(req(TUF_PL2, 90, true, true) && !req(TUF_PL2, 90, true, false));
    assert(!req(TUF_PL2, TUF_PL2_MIN - 1, true, true) && !req(TUF_PL2, TUF_PL2_MAX + 1, true, true));

    // keyboard brightness and RGB
    for (unsigned v = 0; v <= 3; ++v) assert(req(TUF_KBD, v));
    assert(!req(TUF_KBD, 4) && !req(TUF_KBD, 0x80));
    assert(req(TUF_RGB, 0x00ff8000u, true, false, 0) && req(TUF_RGB, 0x02ffffffu, true, false, 2));
    assert(!req(TUF_RGB, 0x03000000u) && !req(TUF_RGB, 0, true, false, 3));
    assert(tuf_rgb_speed_code(0) == 0xe1 && tuf_rgb_speed_code(1) == 0xeb && tuf_rgb_speed_code(2) == 0xf5);

    // never writable: fans, temps, curves via SET, MUX, dGPU, NVIDIA boost/TGP, FPPT, hotkeys
    for (auto device : {TUF_CPU_FAN, TUF_GPU_FAN, TUF_CPU_TEMP, TUF_CPU_CURVE, TUF_GPU_CURVE, TUF_GPU_MUX, TUF_DGPU,
                        0x001200C0u, 0x001200C1u, 0x001200C2u, 0x00100021u, 0x00110018u})
        assert(!req(device, 1, true, true));

    // fan curves
    const uint8_t good[16] = {30, 40, 50, 60, 70, 80, 90, 100, 0, 10, 20, 35, 50, 65, 80, 100};
    assert(tuf_valid_curve(good));
    uint8_t c[16];
    memcpy(c, good, 16); c[7] = 255; assert(!tuf_valid_curve(c));                 // firmware 255 endpoint
    memcpy(c, good, 16); c[3] = c[2] - 1; assert(!tuf_valid_curve(c));            // falling temperature
    const uint8_t silentCpu[16] = {53, 65, 70, 75, 80, 82, 84, 84, 14, 19, 26, 31, 35, 45, 45, 45};   // live FX517ZM data
    const uint8_t silentGpu[16] = {53, 65, 67, 69, 72, 75, 77, 77, 19, 24, 31, 35, 40, 49, 49, 49};
    const uint8_t turboCpu[16] = {30, 55, 61, 66, 69, 72, 75, 78, 14, 26, 31, 35, 45, 56, 70, 91};
    assert(tuf_valid_curve(silentCpu) && tuf_valid_curve(silentGpu) && tuf_valid_curve(turboCpu));
    memcpy(c, good, 16); c[12] = 5; assert(!tuf_valid_curve(c));                  // falling duty
    memcpy(c, good, 16); c[15] = 39; c[14] = 30; assert(!tuf_valid_curve(c));     // top duty below 40 %
    memcpy(c, good, 16); c[15] = 101; assert(!tuf_valid_curve(c));
    memcpy(c, good, 16); c[0] = 19; assert(!tuf_valid_curve(c));
    uint32_t w[4];
    tuf_pack_curve(good, w);
    assert(w[0] == 0x3C32281Eu && w[1] == 0x645A5046u && w[2] == 0x23140A00u && w[3] == 0x64504132u);

    // presence and RAPL model gate
    assert(tuf_present(0x10000) && !tuf_present(0) && !tuf_present(0xfffffffe) && !tuf_present(0xffffffff));
    assert(tuf_rapl_model(6, 0x9A) && tuf_rapl_model(6, 0x97));
    assert(!tuf_rapl_model(6, 0xB7) && !tuf_rapl_model(0x19, 0x9A) && !tuf_rapl_model(6, 0x8C));
    assert(TUF_WORDS == 24 && TUF_W_AC == TUF_WORDS_V1 - 1);
}
