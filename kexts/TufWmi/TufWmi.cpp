// SPDX-License-Identifier: BSD-3-Clause-Clear
// TUF FX517ZM adaptation of thanhnndev's AsusWmi WMNB transport and user-client pattern.
// No AMD MSR/PPT code, raw EC access, GPU switching, NVIDIA Dynamic Boost/TGP writes, hotkey init, arbitrary firmware
// methods or automatic writes. Boot-arg tufwmi=1 starts a read-only bridge; tufwmi=2 additionally permits allowlisted
// DEVS requests (profile, charge limit, fan curves, PL1/PL2, keyboard light), each made only on a user request.
//
// Reads: DSTS of fixed IDs, the EC sensor SEN3._TMP, AC _PSR, and the Intel RAPL energy counter (MSR, read only).
// The SMI-backed presence probes (profile, charge, keyboard, MUX) run once, on the first request, not at start.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <pexpert/pexpert.h>
#include <sys/kauth.h>
#include "tuf_policy.h"

extern "C" int rdmsr_carefully(uint32_t msr, uint32_t *lo, uint32_t *hi);   // 0 on success; no #GP on a missing MSR

#define M_DSTS 0x53545344u
#define M_DEVS 0x53564544u
#define MSR_RAPL_POWER_UNIT 0x606u
#define MSR_PKG_ENERGY_STATUS 0x611u

class TufWmi : public IOService {
    OSDeclareDefaultStructors(TufWmi)
public:
    bool start(IOService *) override;
    void stop(IOService *) override;
    void free() override;
    IOReturn newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **) override;
    IOReturn invoke(uint32_t, IOExternalMethodArguments *);
private:
    IOACPIPlatformDevice *atk = nullptr, *adapter = nullptr, *ec = nullptr, *sen3 = nullptr;
    IOLock *mutex = nullptr;
    bool ready = false, writes = false, rapl = false, probed = false, fanCustom = false, diagDone = false;
    uint32_t caps = 0;
    uint32_t probe[6] = { TUF_UNKNOWN, TUF_UNKNOWN, TUF_UNKNOWN, TUF_UNKNOWN, TUF_UNKNOWN, TUF_UNKNOWN };   // profile, charge, kbd, rgb, mux, dGPU
    uint32_t requested[2] = { TUF_UNKNOWN, TUF_UNKNOWN };
    uint32_t reqPL[2] = { TUF_UNKNOWN, TUF_UNKNOWN };
    uint32_t reqKbd = TUF_UNKNOWN, reqRgb = TUF_UNKNOWN, reqRgbSpeed = TUF_UNKNOWN;
    bool wmnb5(uint32_t method, const uint32_t a[5], OSObject **result);
    bool wmnb(uint32_t method, uint32_t device, uint32_t value, uint32_t *ret);
    uint32_t dsts(uint32_t device);
    bool devs(const uint32_t a[5]);
    bool curveRead(uint32_t fan, uint32_t index, uint8_t out[16]);
    void runProbes();
    uint32_t acPower();
    uint32_t gpuTemp();
    IOACPIPlatformDevice *findDevice(const char *name);
    void fillState(uint32_t state[TUF_WORDS]);
};
OSDefineMetaClassAndStructors(TufWmi, IOService)

// ATKD.WMNB(instance 0, method id, buffer { arg0 .. arg4 }). The buffer must be 20 bytes (CreateDWordField IIA4).
bool TufWmi::wmnb5(uint32_t method, const uint32_t a[5], OSObject **result) {
    OSNumber *instance = OSNumber::withNumber(0ull, 32);
    OSNumber *mid = OSNumber::withNumber((unsigned long long)method, 32);
    OSData *data = OSData::withBytes(a, 5 * sizeof(uint32_t));
    bool ok = false;
    if (instance && mid && data) {
        OSObject *params[3] = { instance, mid, data };
        ok = atk->evaluateObject("WMNB", result, params, 3) == kIOReturnSuccess;
    }
    if (instance) instance->release();
    if (mid) mid->release();
    if (data) data->release();
    return ok;
}

bool TufWmi::wmnb(uint32_t method, uint32_t device, uint32_t value, uint32_t *ret) {
    const uint32_t a[5] = { device, value, 0, 0, 0 };
    OSObject *result = nullptr;
    bool ok = wmnb5(method, a, &result);
    OSNumber *number = result ? OSDynamicCast(OSNumber, result) : nullptr;
    if (ok && number) *ret = number->unsigned32BitValue();
    if (result) result->release();
    return ok && number;
}

uint32_t TufWmi::dsts(uint32_t device) {
    uint32_t raw = TUF_UNKNOWN;
    return wmnb(M_DSTS, device, 0, &raw) ? raw : TUF_UNKNOWN;
}

// ASUS DEVS must explicitly return success (1); zero or unsupported is a failure.
bool TufWmi::devs(const uint32_t a[5]) {
    OSObject *result = nullptr;
    bool ok = wmnb5(M_DEVS, a, &result);
    OSNumber *number = result ? OSDynamicCast(OSNumber, result) : nullptr;
    ok = ok && number && number->unsigned32BitValue() == 1;
    if (result) result->release();
    return ok;
}

// DSTS 0x00110024/25 with arg1 = curve index returns a 32-byte buffer: 8 temperatures, 8 duties, then zero padding.
bool TufWmi::curveRead(uint32_t fan, uint32_t index, uint8_t out[16]) {
    if (fan > 1 || index > 2) return false;
    const uint32_t a[5] = { fan ? TUF_GPU_CURVE : TUF_CPU_CURVE, index, 0, 0, 0 };
    OSObject *result = nullptr;
    bool ok = wmnb5(M_DSTS, a, &result);
    OSData *data = result ? OSDynamicCast(OSData, result) : nullptr;
    ok = ok && data && data->getLength() >= 16;
    if (ok) memcpy(out, data->getBytesNoCopy(), 16);
    if (result) result->release();
    return ok;
}

IOACPIPlatformDevice *TufWmi::findDevice(const char *name) {
    OSDictionary *match = IOService::nameMatching(name);
    if (!match) return nullptr;
    IOService *service = IOService::copyMatchingService(match);
    match->release();
    IOACPIPlatformDevice *device = OSDynamicCast(IOACPIPlatformDevice, service);
    if (service && !device) service->release();
    return device;   // retained, or null
}

uint32_t TufWmi::acPower() {
    // The adapter node may not be published yet when this Root kext starts; retry lazily.
    if (!adapter) adapter = findDevice("ACPI0003");
    UInt32 value = 0;
    if (adapter && adapter->evaluateInteger("_PSR", &value) == kIOReturnSuccess)
        return value == 1 ? 1 : 0;
    return TUF_UNKNOWN; // unknown MUST NOT permit Turbo
}

// EC0.SEN3._TMP ("Remote GT Sensor", EC VRTT, which the NVIDIA platform table uses as the GPU temperature) returns
// deci-Kelvin as VRTT * 10 + 2732, taking the EC mutex itself. 3000 is its "EC not ready" constant, never a reading.
// SEN3 is published as its own IOACPIPlatformDevice under EC0, but evaluating its _TMP returned an error on this Mac
// (0.2.1, TufGpuTempRaw 0). Try, in order: SEN3._TMP on the device, SEN3._TMP from EC0, then the EC byte VRTT itself
// (one EC read, as the BIOS's own DSTS CPU-temperature path does), then its absolute path. The IOReturn of each attempt
// is published as TufGpuTempDiag, and the value that was used as TufGpuTempRaw, for diagnosis.
uint32_t TufWmi::gpuTemp() {
    if (!ec) ec = findDevice("PNP0C09");
    if (ec && !sen3) {
        OSIterator *children = ec->getChildIterator(gIOServicePlane);
        if (children) {
            while (OSObject *child = children->getNextObject()) {
                IOACPIPlatformDevice *device = OSDynamicCast(IOACPIPlatformDevice, child);
                if (device && device->getName() && !strcmp(device->getName(), "SEN3")) { sen3 = device; sen3->retain(); break; }
            }
            children->release();
        }
    }
    UInt32 value = 0;
    IOReturn r[4] = { kIOReturnNotFound, kIOReturnNotFound, kIOReturnNotFound, kIOReturnNotFound };
    uint32_t celsius = TUF_UNKNOWN;
    if (sen3) r[0] = sen3->evaluateInteger("_TMP", &value);
    if (r[0] != kIOReturnSuccess && ec) r[1] = ec->evaluateInteger("SEN3._TMP", &value);
    if (r[0] == kIOReturnSuccess || r[1] == kIOReturnSuccess) {
        // deci-Kelvin, VRTT * 10 + 2732; 3000 is _TMP's "EC not ready" constant, never a reading
        if (value != 3000 && value >= 2732 + 100 && value <= 2732 + 1200) celsius = (value - 2732) / 10;
    } else {
        if (ec) r[2] = ec->evaluateInteger("VRTT", &value);
        if (r[2] != kIOReturnSuccess) r[3] = atk->evaluateInteger("\\_SB.PC00.LPCB.EC0.VRTT", &value);
        if ((r[2] == kIOReturnSuccess || r[3] == kIOReturnSuccess) && value >= 10 && value <= 120) celsius = value;
    }
    if (!diagDone) {
        char diag[96];
        snprintf(diag, sizeof(diag), "sen3=%d %08x %08x %08x %08x", sen3 ? 1 : 0, r[0], r[1], r[2], r[3]);
        setProperty("TufGpuTempDiag", diag);
        diagDone = true;
    }
    setProperty("TufGpuTempRaw", value, 32);
    return celsius;
}

// One-time presence probes. Profile/charge/keyboard go through the BIOS's SMI dispatcher (W15H); reading them every
// refresh would mean an SMI every few seconds, so they are read once and only the EC-backed sensors are polled.
void TufWmi::runProbes() {
    if (probed) return;
    probed = true;
    const uint32_t ids[6] = { TUF_PROFILE, TUF_CHARGE, TUF_KBD, TUF_RGB, TUF_GPU_MUX, TUF_DGPU };
    for (unsigned i = 0; i < 6; ++i) probe[i] = dsts(ids[i]);
    if (tuf_present(probe[0])) caps |= TUF_CAP_PROFILE;
    if (tuf_present(probe[1])) caps |= TUF_CAP_CHARGE;
    if (tuf_present(probe[2])) caps |= TUF_CAP_KBD;
    if (tuf_present(probe[3])) caps |= TUF_CAP_RGB;
    if (probe[4] != TUF_UNKNOWN && probe[4] != TUF_UNSUPPORTED) caps |= TUF_CAP_MUX;
    if (tuf_present(dsts(TUF_PL1)) && tuf_present(dsts(TUF_PL2))) caps |= TUF_CAP_PL;
    uint8_t curve[16];
    if (curveRead(0, TUF_CURVE_PERFORMANCE, curve) && curveRead(1, TUF_CURVE_PERFORMANCE, curve)) caps |= TUF_CAP_FAN_CURVES;
    if (rapl) caps |= TUF_CAP_RAPL;
    IOLog("TufWmi: probes caps 0x%x profile 0x%08x charge 0x%08x kbd 0x%08x rgb 0x%08x mux 0x%08x\n",
          caps, probe[0], probe[1], probe[2], probe[3], probe[4]);
}

void TufWmi::fillState(uint32_t s[TUF_WORDS]) {
    runProbes();
    for (unsigned i = 0; i < TUF_WORDS; ++i) s[i] = TUF_UNKNOWN;
    uint32_t c = caps;
    s[TUF_W_ABI] = TUF_ABI;
    s[TUF_W_RAW_PROFILE] = probe[0];
    s[TUF_W_RAW_CHARGE] = probe[1];
    s[TUF_W_RAW_CPU_FAN] = dsts(TUF_CPU_FAN);
    s[TUF_W_RAW_GPU_FAN] = dsts(TUF_GPU_FAN);
    if (tuf_present(s[TUF_W_RAW_CPU_FAN])) c |= TUF_CAP_CPU_FAN;
    if (tuf_present(s[TUF_W_RAW_GPU_FAN])) c |= TUF_CAP_GPU_FAN;
    s[TUF_W_REQ_PROFILE] = requested[0];
    s[TUF_W_REQ_CHARGE] = requested[1];
    s[TUF_W_WRITES] = writes ? 1u : 0u;
    s[TUF_W_AC] = acPower();
    uint32_t cpu = dsts(TUF_CPU_TEMP);
    if (tuf_present(cpu) && (cpu & 0xff) >= 10 && (cpu & 0xff) <= 120) { s[TUF_W_CPU_TEMP] = cpu & 0xff; c |= TUF_CAP_CPU_TEMP; }
    s[TUF_W_GPU_TEMP] = gpuTemp();
    if (s[TUF_W_GPU_TEMP] != TUF_UNKNOWN) c |= TUF_CAP_GPU_TEMP;
    uint32_t lo = 0, hi = 0;
    if (rapl && rdmsr_carefully(MSR_PKG_ENERGY_STATUS, &lo, &hi) == 0) s[TUF_W_PKG_ENERGY] = lo;
    if (rapl && rdmsr_carefully(MSR_RAPL_POWER_UNIT, &lo, &hi) == 0) s[TUF_W_RAPL_UNIT] = lo;
    s[TUF_W_REQ_PL1] = reqPL[0];
    s[TUF_W_REQ_PL2] = reqPL[1];
    s[TUF_W_FAN_CUSTOM] = fanCustom ? 1u : 0u;
    s[TUF_W_RAW_KBD] = probe[2];
    s[TUF_W_REQ_KBD] = reqKbd;
    s[TUF_W_RAW_RGB] = probe[3];
    s[TUF_W_REQ_RGB] = reqRgb;
    s[TUF_W_REQ_RGB_SPEED] = reqRgbSpeed;
    s[TUF_W_RAW_MUX] = probe[4];
    s[TUF_W_RAW_DGPU] = probe[5];
    s[TUF_W_CAPS] = c;
}

bool TufWmi::start(IOService *provider) {
    int optIn = 0;
    if (!PE_parse_boot_argn("tufwmi", &optIn, sizeof(optIn)) || !tuf_bridge_enabled(optIn)) return false;
    if (!IOService::start(provider)) return false;
    OSString *model = OSDynamicCast(OSString, getProperty("TufModel"));
    atk = OSDynamicCast(IOACPIPlatformDevice, provider);
    if (!model || !model->isEqualTo("FX517ZM") || !atk) { IOService::stop(provider); return false; }
    // Confirm the ASUS WMNB method GUID in the provider's read-only _WDG table.
    OSObject *wdg = nullptr;
    if (atk->evaluateObject("_WDG", &wdg) != kIOReturnSuccess) { IOService::stop(provider); return false; }
    OSData *table = OSDynamicCast(OSData, wdg);
    const uint8_t guid[16] = {0xd0,0x5e,0x84,0x97,0x6d,0x4e,0xde,0x11,0x8a,0x39,0x08,0x00,0x20,0x0c,0x9a,0x66};
    bool found = false;
    if (table) {
        const uint8_t *bytes = (const uint8_t *)table->getBytesNoCopy();
        for (unsigned i = 0; i + 20 <= table->getLength(); i += 20)
            if (!memcmp(bytes + i, guid, 16) && bytes[i+16] == 'N' && bytes[i+17] == 'B' && (bytes[i+19] & 2)) found = true;
    }
    if (wdg) wdg->release();
    if (!found) { IOService::stop(provider); return false; }
    mutex = IOLockAlloc();
    if (!mutex) { IOService::stop(provider); return false; }
    // RAPL: GenuineIntel family 6 Alder Lake only.
    uint32_t a = 0, b = 0, c = 0, d = 0;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    if (b == 0x756e6547 && d == 0x49656e69 && c == 0x6c65746e) {
        a = 1; c = 0;
        __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
        uint32_t family = (a >> 8) & 0xf, cpuModel = ((a >> 4) & 0xf) | ((a >> 12) & 0xf0);
        rapl = tuf_rapl_model(family, cpuModel);
    }
    adapter = findDevice("ACPI0003");
    writes = tuf_writes_enabled(optIn);
    ready = true;
    setProperty("TufABI", TUF_ABI, 32);
    setProperty("TufWritesEnabled", writes);
    registerService();
    IOLog("TufWmi: FX517ZM bridge ready (%s, RAPL %s); no startup firmware calls\n", writes ? "writes opt-in" : "read-only", rapl ? "on" : "off");
    return true;
}
void TufWmi::stop(IOService *provider) {
    if (mutex) { IOLockLock(mutex); ready = false; IOLockUnlock(mutex); }
    IOService::stop(provider);
}
void TufWmi::free() {
    if (adapter) { adapter->release(); adapter = nullptr; }
    if (ec) { ec->release(); ec = nullptr; }
    if (sen3) { sen3->release(); sen3 = nullptr; }
    if (mutex) { IOLockFree(mutex); mutex = nullptr; }
    IOService::free();
}

IOReturn TufWmi::invoke(uint32_t selector, IOExternalMethodArguments *a) {
    if (!a || !mutex) return kIOReturnBadArgument;
    if (a->asyncWakePort != MACH_PORT_NULL) return kIOReturnUnsupported;
    IOLockLock(mutex);
    IOReturn status = kIOReturnUnsupported;
    if (!ready || isInactive()) { IOLockUnlock(mutex); return kIOReturnNotReady; }
    if (selector == TUF_SEL_STATE) {
        // ABI 1 clients ask for 10 words, ABI 2 clients for TUF_WORDS; each gets that many.
        if (a->scalarInputCount || a->structureInputSize || !a->structureOutput || a->structureOutputSize < TUF_WORDS_V1 * 4 || a->scalarOutputCount) {
            status = kIOReturnBadArgument;
        } else {
            uint32_t state[TUF_WORDS];
            fillState(state);
            uint32_t words = a->structureOutputSize / 4 < TUF_WORDS ? a->structureOutputSize / 4 : TUF_WORDS;
            memcpy(a->structureOutput, state, words * 4);
            a->structureOutputSize = words * 4;
            status = kIOReturnSuccess;
        }
    } else if (selector == TUF_SEL_SET) {
        runProbes();
        const uint32_t n = a->scalarInputCount;
        if ((n != 2 && n != 3) || !a->scalarInput || a->structureInputSize || a->scalarOutputCount || a->structureOutputSize) {
            status = kIOReturnBadArgument;
        } else if (!writes) {
            status = kIOReturnNotPermitted;
        } else {
            const uint64_t device = a->scalarInput[0], value = a->scalarInput[1], value2 = n == 3 ? a->scalarInput[2] : 0;
            const uint32_t need = device == TUF_PROFILE ? TUF_CAP_PROFILE : device == TUF_CHARGE ? TUF_CAP_CHARGE
                                : device == TUF_PL1 || device == TUF_PL2 ? TUF_CAP_PL : device == TUF_KBD ? TUF_CAP_KBD
                                : device == TUF_RGB ? TUF_CAP_RGB : 0;
            if (!tuf_valid_request(device, value, value2, acPower() == 1, fanCustom)) {
                status = kIOReturnBadArgument;
            } else if (!need || !(caps & need)) {
                status = kIOReturnUnsupported;
            } else {
                const uint32_t dev = (uint32_t)device, v = (uint32_t)value, v2 = (uint32_t)value2;
                uint32_t args[5] = { dev, v, 0, 0, 0 };
                if (dev == TUF_KBD) args[1] = 0x80u | v;                           // asus-wmi kbd_led_update
                if (dev == TUF_RGB) {                                                // asus-wmi kbd_rgb_mode: 0xb3 = set, not saved
                    args[1] = 0xb3u | ((v >> 24) << 8) | ((v & 0xff) << 16) | (((v >> 8) & 0xff) << 24);
                    args[2] = ((v >> 16) & 0xff) | (tuf_rgb_speed_code(v2) << 8);
                }
                if (devs(args)) {
                    if (dev == TUF_PROFILE) {
                        // A profile request reloads the firmware's own curves and limits (asus-wmi's way to end custom fans).
                        requested[0] = v; fanCustom = false; reqPL[0] = reqPL[1] = TUF_UNKNOWN;
                    } else if (dev == TUF_CHARGE) requested[1] = v;
                    else if (dev == TUF_PL1) reqPL[0] = v;
                    else if (dev == TUF_PL2) reqPL[1] = v;
                    else if (dev == TUF_KBD) reqKbd = v;
                    else { reqRgb = v; reqRgbSpeed = v2; }
                    status = kIOReturnSuccess;
                } else status = kIOReturnIOError;
            }
        }
    } else if (selector == TUF_SEL_CURVE_READ) {
        runProbes();
        if (a->scalarInputCount != 2 || !a->scalarInput || a->structureInputSize || a->scalarOutputCount || !a->structureOutput || a->structureOutputSize < 16) {
            status = kIOReturnBadArgument;
        } else if (!(caps & TUF_CAP_FAN_CURVES)) {
            status = kIOReturnUnsupported;
        } else if (a->scalarInput[0] > 1 || a->scalarInput[1] > 2) {
            status = kIOReturnBadArgument;
        } else {
            uint8_t curve[16];
            if (curveRead((uint32_t)a->scalarInput[0], (uint32_t)a->scalarInput[1], curve)) {
                memcpy(a->structureOutput, curve, 16);
                a->structureOutputSize = 16;
                status = kIOReturnSuccess;
            } else status = kIOReturnIOError;
        }
    } else if (selector == TUF_SEL_CURVE_WRITE) {
        runProbes();
        if (a->scalarInputCount != 1 || !a->scalarInput || a->structureInputSize != 16 || !a->structureInput || a->scalarOutputCount || a->structureOutputSize) {
            status = kIOReturnBadArgument;
        } else if (!writes) {
            status = kIOReturnNotPermitted;
        } else if (!(caps & TUF_CAP_FAN_CURVES)) {
            status = kIOReturnUnsupported;
        } else {
            uint8_t curve[16];
            memcpy(curve, a->structureInput, 16);
            const uint64_t fan = a->scalarInput[0];
            if (fan > 1 || !tuf_valid_curve(curve)) {
                status = kIOReturnBadArgument;
            } else {
                uint32_t args[5] = { fan ? TUF_GPU_CURVE : TUF_CPU_CURVE, 0, 0, 0, 0 };
                tuf_pack_curve(curve, args + 1);
                if (devs(args)) {
                    if (fan == 0) fanCustom = true;   // the CPU curve write sets the EC's manual-fan flag (FMAE)
                    status = kIOReturnSuccess;
                } else status = kIOReturnIOError;
            }
        }
    } else if (selector == TUF_SEL_FAN_RESET) {
        runProbes();
        if (a->scalarInputCount || a->structureInputSize || a->scalarOutputCount || a->structureOutputSize) {
            status = kIOReturnBadArgument;
        } else if (!writes) {
            status = kIOReturnNotPermitted;
        } else if (!(caps & TUF_CAP_PROFILE)) {
            status = kIOReturnUnsupported;
        } else {
            // Request the profile again (Turbo falls back to Performance off AC) so the firmware reloads its own fans.
            uint32_t profile = requested[0] <= 2 ? requested[0] : 0;
            if (profile == 1 && acPower() != 1) profile = 0;
            const uint32_t args[5] = { TUF_PROFILE, profile, 0, 0, 0 };
            if (devs(args)) {
                requested[0] = profile; fanCustom = false; reqPL[0] = reqPL[1] = TUF_UNKNOWN;
                status = kIOReturnSuccess;
            } else status = kIOReturnIOError;
        }
    }
    IOLockUnlock(mutex);
    return status;
}

class TufWmiUserClient : public IOUserClient {
    OSDeclareDefaultStructors(TufWmiUserClient)
    TufWmi *bridge = nullptr;
    bool authorized = false;
public:
    bool initWithTask(task_t task, void *token, UInt32 type, OSDictionary *props) override {
        if (!IOUserClient::initWithTask(task, token, type, props)) return false;
        // Apple's "Administrator" IOKit privilege means UID 0, not membership in admin.
        // IOServiceOpen runs this on the caller's thread, so kauth_cred_get() is the caller's
        // credential; require owningTask to be that same caller so every check has one subject.
        // "console" is UID-based: any process of the logged-in console user passes it
        // (including that user's SSH sessions and agents), not only the foreground session.
        const bool sameTask = task == current_task();
        const bool root = clientHasPrivilege(task, kIOClientPrivilegeAdministrator) == kIOReturnSuccess;
        const bool console = clientHasPrivilege(task, "console") == kIOReturnSuccess;
        int member = 0;
        const bool admin = kauth_cred_ismember_gid(kauth_cred_get(), 80, &member) == 0 && member != 0;
        authorized = tuf_authorized(sameTask, root, console, admin);
        return authorized;
    }
    bool start(IOService *provider) override {
        TufWmi *p = OSDynamicCast(TufWmi, provider);
        if (!p || !IOUserClient::start(provider)) return false;
        bridge = p; bridge->retain(); return true;
    }
    void free() override { if (bridge) { bridge->release(); bridge = nullptr; } IOUserClient::free(); }
    IOReturn clientClose() override { terminate(); return kIOReturnSuccess; }
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *a, IOExternalMethodDispatch *, OSObject *, void *) override {
        if (!authorized || !bridge || isInactive()) return kIOReturnNotPermitted;
        return bridge->invoke(selector, a);
    }
};
OSDefineMetaClassAndStructors(TufWmiUserClient, IOUserClient)
IOReturn TufWmi::newUserClient(task_t task, void *token, UInt32 type, OSDictionary *props, IOUserClient **handler) {
    if (!handler) return kIOReturnBadArgument;
    *handler = nullptr;
    if (type != 0) return kIOReturnBadArgument;
    TufWmiUserClient *client = OSTypeAlloc(TufWmiUserClient);
    if (!client) return kIOReturnNoMemory;
    if (!client->initWithTask(task, token, type, props)) { client->release(); return kIOReturnNotPrivileged; }
    if (!client->attach(this)) { client->release(); return kIOReturnError; }
    if (!client->start(this)) { client->detach(this); client->release(); return kIOReturnError; }
    *handler = client; return kIOReturnSuccess;
}
