// ItlDiag - boot-time diagnostics. Dumps what macOS sees on PCI/USB to the verbose console and holds the boot
// for a while so the screen can be photographed. Matches IOResources, so it loads regardless of any hardware.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <pexpert/pexpert.h>
#include <string.h>

class ItlDiag : public IOService {
    OSDeclareDefaultStructors(ItlDiag)
public:
    bool start(IOService *provider) override;
};

#define super IOService
OSDefineMetaClassAndStructors(ItlDiag, IOService)

#define MAXL 90
static char lines[MAXL][110];
static int nl;

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

static bool hasStr(const char *h, const char *n)
{
    if (!h) return false;
    size_t nl_ = strlen(n);
    for (; *h; h++) if (!strncmp(h, n, nl_)) return true;
    return false;
}

static void collect()
{
    nl = 0;
    IORegistryIterator *it = IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively);
    if (!it) return;
    IORegistryEntry *e;
    while ((e = it->getNextObject())) {
        const char *cls = e->getMetaClass()->getClassName();
        IOPCIDevice *p = OSDynamicCast(IOPCIDevice, e);
        if (p) {
            unsigned v = p->configRead16(kIOPCIConfigVendorID);
            unsigned d = p->configRead16(kIOPCIConfigDeviceID);
            unsigned c = p->configRead32(kIOPCIConfigRevisionID) >> 8;
            // %s=location(unused arg a) ; keep to one short line
            char tmp[110];
            snprintf(tmp, sizeof(tmp), "pci %04x:%04x c%06x %s -> %s", v, d, c, e->getName(), firstChildClass(e));
            if (nl < MAXL) { strlcpy(lines[nl++], tmp, sizeof(lines[0])); }
            if (v == 0x14c3) {
                OSData *vid = OSDynamicCast(OSData, e->getProperty("vendor-id"));
                OSData *did = OSDynamicCast(OSData, e->getProperty("device-id"));
                OSString *pm = OSDynamicCast(OSString, e->getProperty("IOPCIMatch"));
                OSString *pm2 = OSDynamicCast(OSString, e->getProperty("IOPCIPrimaryMatch"));
                snprintf(tmp, sizeof(tmp), "  MT: vid=%x did=%x match=%s prim=%s",
                         vid && vid->getLength() >= 2 ? *(const uint16_t *)vid->getBytesNoCopy() : 0,
                         did && did->getLength() >= 2 ? *(const uint16_t *)did->getBytesNoCopy() : 0,
                         pm ? pm->getCStringNoCopy() : "-", pm2 ? pm2->getCStringNoCopy() : "-");
                if (nl < MAXL) strlcpy(lines[nl++], tmp, sizeof(lines[0]));
            }
        } else if (hasStr(cls, "XHCI") || hasStr(cls, "HIDKeyboard") || hasStr(cls, "RNDIS") || hasStr(cls, "HoRNDIS")
                   || !strcmp(cls, "IOUSBHostDevice") || hasStr(cls, "VoodooI2C")) {
            char tmp[110];
            snprintf(tmp, sizeof(tmp), "reg %s (%s)", cls, e->getName());
            if (nl < MAXL) strlcpy(lines[nl++], tmp, sizeof(lines[0]));
        }
    }
    it->release();
}

bool ItlDiag::start(IOService *provider)
{
    if (!super::start(provider)) return false;

    uint32_t hold = 0;
    if (!PE_parse_boot_argn("itldiag", &hold, sizeof(hold)) || !hold) return true;

    IOLog("ItlDiag: armed, waiting 10s for PCI/USB enumeration\n");
    IOSleep(10000);
    for (uint32_t t = 0; t < hold; t += 10) {
        collect();
        IOLog("ItlDiag: ===== dump at +%us, %d lines =====\n", 10 + t, nl);
        for (int i = 0; i < nl; i++) IOLog("D%02d %s\n", i, lines[i]);
        IOLog("ItlDiag: ===== end =====\n");
        IOSleep(10000);
    }
    return true;
}
