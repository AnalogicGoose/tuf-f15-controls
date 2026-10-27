// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <stdint.h>
// ABI 2 (TufWmi 0.2.x). ABI 1 clients read only the first TUF_WORDS_V1 words.
#define TUF_ABI 2u
#define TUF_PRESENT 0x00010000u
#define TUF_UNKNOWN 0xffffffffu
#define TUF_UNSUPPORTED 0xfffffffeu

// WMNB DSTS/DEVS device IDs, each confirmed in the FX517ZM DSDT (work/brightness/dsl) unless noted.
#define TUF_PROFILE 0x00120075u     // throttle_thermal_policy, generic SMI path (WCHK/W15H)
#define TUF_CHARGE 0x00120057u      // RSOC charge end, generic SMI path
#define TUF_CPU_FAN 0x00110013u     // EC FRMS -> units of 100 RPM
#define TUF_GPU_FAN 0x00110014u     // EC FRS2
#define TUF_CPU_TEMP 0x00120094u    // EC CTMP, °C, DSTS only
#define TUF_CPU_CURVE 0x00110024u   // DSTS(arg1 = 0 Performance, 1 Silent, 2 Turbo) -> EC CFCV/CSFV/CTFV;
#define TUF_GPU_CURVE 0x00110025u   //   DEVS -> SFTC/SFTG (CPU DEVS also sets EC FMAE, manual fans)
#define TUF_PL1 0x001200A3u         // DEVS -> EC PL1V (W)
#define TUF_PL2 0x001200A0u         // DEVS -> EC PL2V (W)
#define TUF_GPU_MUX 0x00090016u     // DSTS only here: never written
#define TUF_DGPU 0x00090020u        // DSTS only here: never written
#define TUF_KBD 0x00050021u         // keyboard backlight, not in the DSDT: SMI path, probed at runtime
#define TUF_RGB 0x00100056u         // TUF RGB keyboard mode, not in the DSDT: SMI path, probed at runtime

// Fan curve index for DSTS 0x00110024/25 (firmware method order CFCV, CSFV, CTFV). Differs from the profile values.
#define TUF_CURVE_PERFORMANCE 0u
#define TUF_CURVE_SILENT 1u
#define TUF_CURVE_TURBO 2u

// Manual power limits (W). i7-12650H: 45 W base, 115 W max turbo; kept well inside both.
#define TUF_PL1_MIN 15u
#define TUF_PL1_MAX 65u
#define TUF_PL2_MIN 25u
#define TUF_PL2_MAX 95u

// Wire state, uint32_t words.
enum {
    TUF_W_ABI = 0, TUF_W_CAPS,
    TUF_W_RAW_PROFILE, TUF_W_RAW_CHARGE, TUF_W_RAW_CPU_FAN, TUF_W_RAW_GPU_FAN,
    TUF_W_REQ_PROFILE, TUF_W_REQ_CHARGE, TUF_W_WRITES, TUF_W_AC,          // end of ABI 1
    TUF_W_CPU_TEMP,        // °C or TUF_UNKNOWN
    TUF_W_GPU_TEMP,        // °C (EC VRTT via SEN3._TMP) or TUF_UNKNOWN
    TUF_W_PKG_ENERGY,      // MSR_PKG_ENERGY_STATUS low 32 bits, or TUF_UNKNOWN
    TUF_W_RAPL_UNIT,       // MSR_RAPL_POWER_UNIT low 32 bits (energy unit = 1 / 2^bits[12:8] J), or TUF_UNKNOWN
    TUF_W_REQ_PL1, TUF_W_REQ_PL2,   // W last accepted since the last profile request, TUF_UNKNOWN none
    TUF_W_FAN_CUSTOM,      // 1: a custom curve was accepted since the last profile request
    TUF_W_RAW_KBD, TUF_W_REQ_KBD,
    TUF_W_RAW_RGB, TUF_W_REQ_RGB, TUF_W_REQ_RGB_SPEED,
    TUF_W_RAW_MUX, TUF_W_RAW_DGPU,
    TUF_WORDS
};
#define TUF_WORDS_V1 10u

// Capability bits in TUF_W_CAPS
enum {
    TUF_CAP_PROFILE = 1u << 0, TUF_CAP_CHARGE = 1u << 1, TUF_CAP_CPU_FAN = 1u << 2, TUF_CAP_GPU_FAN = 1u << 3,
    TUF_CAP_CPU_TEMP = 1u << 4, TUF_CAP_GPU_TEMP = 1u << 5, TUF_CAP_RAPL = 1u << 6,
    TUF_CAP_FAN_CURVES = 1u << 7, TUF_CAP_PL = 1u << 8, TUF_CAP_KBD = 1u << 9, TUF_CAP_RGB = 1u << 10,
    TUF_CAP_MUX = 1u << 11,
};

// User client selectors
enum { TUF_SEL_STATE = 0, TUF_SEL_SET = 1, TUF_SEL_CURVE_READ = 2, TUF_SEL_CURVE_WRITE = 3, TUF_SEL_FAN_RESET = 4 };

static inline bool tuf_present(uint32_t raw) {
    return raw != TUF_UNSUPPORTED && raw != TUF_UNKNOWN && (raw & TUF_PRESENT);
}

// Keyboard RGB request: value = red | green << 8 | blue << 16 | mode << 24 (0 static, 1 breathing, 2 colour cycle),
// value2 = speed index 0..2 (asus-wmi 0xe1 / 0xeb / 0xf5).
static inline bool tuf_valid_rgb(uint64_t value, uint64_t speed) {
    return value <= 0x02ffffffu && speed <= 2;
}
static inline uint32_t tuf_rgb_speed_code(uint32_t speed) { return speed == 0 ? 0xe1u : speed == 1 ? 0xebu : 0xf5u; }

// Validate the original 64-bit user arguments BEFORE narrowing to firmware dwords.
// fanCustom: PL1/PL2 only take effect in the EC's manual-fan mode, so they are refused without a custom curve.
static inline bool tuf_valid_request(uint64_t device, uint64_t value, uint64_t value2, bool ac, bool fanCustom) {
    if (device == TUF_RGB) return tuf_valid_rgb(value, value2);
    if (value2 != 0) return false;
    if (device == TUF_PROFILE) return value <= 2 && (value != 1 || ac);
    if (device == TUF_CHARGE) return value == 60 || value == 80 || value == 100;
    if (device == TUF_PL1) return fanCustom && value >= TUF_PL1_MIN && value <= TUF_PL1_MAX;
    if (device == TUF_PL2) return fanCustom && value >= TUF_PL2_MIN && value <= TUF_PL2_MAX;
    if (device == TUF_KBD) return value <= 3;
    return false;
}

// Custom curve: 8 temperatures (°C) then 8 duties (%). Temperatures not falling within 20..105 (the FX517ZM's own
// Silent curves repeat their last temperature: 84,84 CPU / 77,77 GPU), duties not falling, and at least 40 % at the
// top point.
static inline bool tuf_valid_curve(const uint8_t c[16]) {
    for (int i = 0; i < 8; i++) {
        if (c[i] < 20 || c[i] > 105 || c[8 + i] > 100) return false;
        if (i && (c[i] < c[i - 1] || c[8 + i] < c[8 + i - 1])) return false;
    }
    return c[15] >= 40;
}
// DEVS args 1..4: temperatures 0-3, 4-7, duties 0-3, 4-7, low byte first (asus-wmi fan_curve_write).
static inline void tuf_pack_curve(const uint8_t c[16], uint32_t out[4]) {
    for (int w = 0; w < 4; w++) {
        out[w] = 0;
        for (int i = 0; i < 4; i++) out[w] |= (uint32_t)c[4 * w + i] << (8 * i);
    }
}

// Every check must describe the process that called IOServiceOpen; a caller-chosen
// owningTask that differs from the calling task is rejected outright.
static inline bool tuf_authorized(bool sameTask, bool root, bool console, bool admin) {
    return sameTask && (root || (console && admin));
}

// Boot-arg tufwmi: 1 = read-only bridge, 2 = bridge plus allowlisted DEVS writes.
static inline bool tuf_bridge_enabled(int optIn) { return optIn == 1 || optIn == 2; }
static inline bool tuf_writes_enabled(int optIn) { return optIn == 2; }

// Intel RAPL MSRs are read only on Alder Lake family 6 models (no #GP risk on other CPUs or VMs).
static inline bool tuf_rapl_model(uint32_t family, uint32_t model) {
    return family == 6 && (model == 0x97 || model == 0x9A);
}
