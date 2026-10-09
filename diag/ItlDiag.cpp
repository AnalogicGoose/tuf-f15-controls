// ItlDiag - boot-time diagnostics. Dumps what macOS sees (Wi-Fi card, USB controllers/devices, HID, clocks) to the
// verbose console and holds the boot for a while so the screen can be photographed. Matches IOResources, so it loads
// regardless of any hardware.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include <stdarg.h>
#include <string.h>
#include <libkern/libkern.h>

class ItlDiag : public IOService {
    OSDeclareDefaultStructors(ItlDiag)
public:
    bool start(IOService *provider) override;
};

#define super IOService
OSDefineMetaClassAndStructors(ItlDiag, IOService)

#define MAXL 60
#define LW 112
static char lines[MAXL][LW];
static int nl;

static uint64_t tsc0, abs0ns, cal0us;

static inline uint64_t rdtsc_(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t absNs(void)
{
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns;
}

static uint64_t calUs(void)
{
    clock_sec_t s;
    clock_usec_t u;
    clock_get_calendar_microtime(&s, &u);
    return (uint64_t)s * 1000000ull + u;
}

static void add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void add(const char *fmt, ...)
{
    if (nl >= MAXL) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lines[nl], LW, fmt, ap);
    va_end(ap);
    nl++;
}

static const char *firstChildClass(IORegistryEntry *e)
{
    const char *r = "-";
    OSIterator *ci = e->getChildIterator(gIOServicePlane);
    if (ci) {
        IORegistryEntry *c = OSDynamicCast(IORegistryEntry, ci->getNextObject());
        if (c) r = c->getMetaClass()->getClassName();
        ci->release();
    }
    return r;
}

static IORegistryEntry *firstChild(IORegistryEntry *e)
{
    IORegistryEntry *r = nullptr;
    OSIterator *ci = e->getChildIterator(gIOServicePlane);
    if (ci) {
        r = OSDynamicCast(IORegistryEntry, ci->getNextObject());
        if (r) r->retain();
        ci->release();
    }
    return r;
}

static bool hasStr(const char *h, const char *n)
{
    if (!h) return false;
    size_t nl_ = strlen(n);
    for (; *h; h++) if (!strncmp(h, n, nl_)) return true;
    return false;
}

static unsigned num(IORegistryEntry *e, const char *key)
{
    OSNumber *n = OSDynamicCast(OSNumber, e->getProperty(key));
    return n ? (unsigned)n->unsigned32BitValue() : 0xffffffffu;
}

static const char *str(IORegistryEntry *e, const char *key)
{
    OSString *s = OSDynamicCast(OSString, e->getProperty(key));
    return s ? s->getCStringNoCopy() : "-";
}

static void collect()
{
    nl = 0;

    // Clock sanity: three independent time sources over the same interval. If kernel time (mach) disagrees with the
    // RTC (cal) the key-repeat / timer based symptoms follow.
    uint64_t dCal = (calUs() - cal0us) / 1000;                 // ms, RTC
    uint64_t dAbs = (absNs() - abs0ns) / 1000000ull;           // ms, mach_absolute_time
    uint64_t dTsc = rdtsc_() - tsc0;
    add("clk cal=%llums mach=%llums tsc=%lluMHz | pe cpu=%lluMHz bus=%lluMHz tb=%luMHz",
        dCal, dAbs, dCal ? dTsc / (dCal * 1000ull) : 0ull,
        gPEClockFrequencyInfo.cpu_frequency_hz / 1000000ull, gPEClockFrequencyInfo.bus_frequency_hz / 1000000ull,
        gPEClockFrequencyInfo.timebase_frequency_hz / 1000000ul);

    IORegistryIterator *it = IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively);
    if (!it) return;
    IORegistryEntry *e;
    int nusb = 0, nhid = 0;
    while ((e = it->getNextObject())) {
        const char *cls = e->getMetaClass()->getClassName();
        IOPCIDevice *p = OSDynamicCast(IOPCIDevice, e);
        if (p) {
            unsigned v = p->configRead16(kIOPCIConfigVendorID);
            unsigned d = p->configRead16(kIOPCIConfigDeviceID);
            unsigned c = p->configRead32(kIOPCIConfigRevisionID) >> 8;
            if (v == 0x14c3) {
                add("wifi %04x:%04x c%06x %s -> %s", v, d, c, e->getName(), firstChildClass(e));
                IORegistryEntry *ch = firstChild(e);
                if (ch) {
                    // m0-log is up to ~240 chars; show it over three lines
                    const char *m = str(ch, "m0-log");
                    size_t n = strlen(m);
                    for (size_t o = 0; o < n && o < 240; o += 78) {
                        char seg[80];
                        strlcpy(seg, m + o, sizeof(seg));
                        add("  m0: %s", seg);
                    }
                    ch->release();
                }
            } else if ((c >> 8) == 0x0c03) {                    // USB controller
                add("xhc %04x:%04x %s -> %s", v, d, e->getName(), firstChildClass(e));
            }
        } else if (!strcmp(cls, "IOUSBHostDevice")) {
            nusb++;
            add("usb %04x:%04x %s loc=%08x spd=%u", num(e, "idVendor") & 0xffff, num(e, "idProduct") & 0xffff,
                e->getName(), num(e, "locationID"), num(e, "Device Speed"));
        } else if (hasStr(cls, "HIDDevice") || hasStr(cls, "HIDKeyboard")) {
            nhid++;
            add("hid %s v=%x p=%x %s tr=%s", cls, num(e, "VendorID") & 0xffff, num(e, "ProductID") & 0xffff,
                str(e, "Product"), str(e, "Transport"));
        } else if (hasStr(cls, "RNDIS")) {
            add("net %s (%s)", cls, e->getName());
        }
    }
    it->release();
    add("summary: usb devices=%d hid devices=%d", nusb, nhid);
}

bool ItlDiag::start(IOService *provider)
{
    if (!super::start(provider)) return false;

    uint32_t hold = 0;
    if (!PE_parse_boot_argn("itldiag", &hold, sizeof(hold)) || !hold) return true;

    tsc0 = rdtsc_();
    abs0ns = absNs();
    cal0us = calUs();

    IOLog("ItlDiag: armed, waiting 10s for PCI/USB enumeration\n");
    IOSleep(10000);
    for (uint32_t t = 0; t < hold; t += 15) {
        collect();
        IOLog("ItlDiag: ===== dump at +%us, %d lines =====\n", 10 + t, nl);
        for (int i = 0; i < nl; i++) IOLog("%s\n", lines[i]);
        IOLog("ItlDiag: ===== end =====\n");
        IOSleep(15000);
    }
    return true;
}
