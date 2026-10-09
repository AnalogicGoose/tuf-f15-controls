// ItlDiag - boot-time diagnostics. Collects what macOS sees (Wi-Fi card, USB controllers/devices, HID, clocks, the whole
// registry tree) and persists it to a pre-allocated file on the boot USB stick (see scripts/usblog.ps1), so no screenshots
// or working keyboard are needed. The same lines also go to the verbose console.
//
// How the file is found: the stick is FAT32; ItlDiag reads each partition's BPB, looks up the root-directory entry
// ITLLOG.BIN, takes its first cluster and writes straight through the partition's IOMedia into the file's clusters.
// Windows created the file contiguous and put "ITLLOG01" in the first sector and "ITLEND01" in the last; both are
// verified before anything is written.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/storage/IOMedia.h>
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

// ---------------------------------------------------------------- text buffer (mirrored to the USB file)

#define TXT_MAX (768 * 1024)
static char *txt;
static size_t tlen;
static bool tfull;

static void rawAppend(const char *s, size_t n)
{
    if (!txt) return;
    if (tlen + n + 1 >= TXT_MAX) {
        if (!tfull) { tfull = true; const char *m = "\n[buffer full]\n"; size_t k = strlen(m); memcpy(txt + tlen, m, k); tlen += k; }
        return;
    }
    memcpy(txt + tlen, s, n);
    tlen += n;
}

static void fileLine(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void fileLine(const char *fmt, ...)
{
    char b[400];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof(b)) n = sizeof(b) - 1;
    rawAppend(b, n);
    rawAppend("\n", 1);
}

// to console and file
static void outLine(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void outLine(const char *fmt, ...)
{
    char b[400];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof(b)) n = sizeof(b) - 1;
    IOLog("%s\n", b);
    rawAppend(b, n);
    rawAppend("\n", 1);
}

// ---------------------------------------------------------------- synchronous raw I/O on an IOMedia

struct IoWait { volatile int done; IOReturn status; UInt64 actual; };

static void ioDone(void *, void *parameter, IOReturn status, UInt64 actual)
{
    IoWait *w = (IoWait *)parameter;
    w->status = status;
    w->actual = actual;
    w->done = 1;
}

static IOReturn mediaIO(IOMedia *m, IOService *client, bool wr, UInt64 off, void *data, size_t len)
{
    IOBufferMemoryDescriptor *md = IOBufferMemoryDescriptor::withCapacity(len, wr ? kIODirectionOut : kIODirectionIn);
    if (!md) return kIOReturnNoMemory;
    if (wr) memcpy(md->getBytesNoCopy(), data, len);
    IoWait *w = (IoWait *)IOMalloc(sizeof(IoWait));
    if (!w) { md->release(); return kIOReturnNoMemory; }
    w->done = 0; w->status = kIOReturnError; w->actual = 0;
    IOStorageCompletion c = { nullptr, ioDone, w };
    IOStorageAttributes a;
    bzero(&a, sizeof(a));
    if (wr) m->write(client, off, md, &a, &c); else m->read(client, off, md, &a, &c);
    for (int i = 0; i < 3000 && !w->done; i++) IOSleep(1);
    if (!w->done) return kIOReturnTimeout;          // leak md/w: the device may still touch them
    IOReturn r = w->status;
    if (r == kIOReturnSuccess && w->actual != len) r = kIOReturnUnderrun;
    if (r == kIOReturnSuccess && !wr) memcpy(data, md->getBytesNoCopy(), len);
    IOFree(w, sizeof(IoWait));
    md->release();
    return r;
}

// ---------------------------------------------------------------- locating ITLLOG.BIN on a FAT32 partition

static IOMedia *logMedia;            // opened read/write once found
static IOService *logClient;         // the client that opened it
static UInt64 logStart;              // byte offset (within logMedia) of the first log byte (after the header sector)
static UInt64 logCap;                // usable bytes
static size_t flushedSec;            // sectors of txt already written
static char attachMsg[120] = "not tried";

static unsigned le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static unsigned le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

static bool probeMedia(IOMedia *m, IOService *me, UInt64 *fileOff, UInt64 *fileSize, char *why, size_t whySz)
{
    uint8_t sec[512];
    if (mediaIO(m, me, false, 0, sec, 512) != kIOReturnSuccess) { snprintf(why, whySz, "rd0 fail"); return false; }
    if (sec[510] != 0x55 || sec[511] != 0xaa || le16(sec + 11) != 512 || strncmp((char *)sec + 0x52, "FAT32", 5)) {
        snprintf(why, whySz, "not fat32");
        return false;
    }
    unsigned spc = sec[13], res = le16(sec + 14), nfat = sec[16], fatsz = le32(sec + 0x24), root = le32(sec + 0x2c);
    if (!spc || !nfat || root < 2) { snprintf(why, whySz, "bad bpb"); return false; }
    UInt64 dataStart = (UInt64)(res + (UInt64)nfat * fatsz) * 512;
    UInt64 cb = (UInt64)spc * 512;
    if (cb > 65536) { snprintf(why, whySz, "cluster too big"); return false; }
    uint8_t *dir = (uint8_t *)IOMalloc((size_t)cb);
    if (!dir) return false;
    bool found = false;
    UInt64 clus = 0, size = 0;
    if (mediaIO(m, me, false, dataStart + (root - 2) * cb, dir, (size_t)cb) == kIOReturnSuccess) {
        for (size_t o = 0; o + 32 <= cb; o += 32) {
            if (dir[o] == 0) break;
            if (dir[o] == 0xe5 || (dir[o + 11] & 0x0f) == 0x0f) continue;
            if (!memcmp(dir + o, "ITLLOG  BIN", 11)) {
                clus = ((UInt64)le16(dir + o + 0x14) << 16) | le16(dir + o + 0x1a);
                size = le32(dir + o + 0x1c);
                found = true;
                break;
            }
        }
    }
    IOFree(dir, (size_t)cb);
    if (!found) { snprintf(why, whySz, "no ITLLOG.BIN in root"); return false; }
    if (clus < 2 || size < 4096) { snprintf(why, whySz, "bad entry clus=%llu size=%llu", clus, size); return false; }
    UInt64 off = dataStart + (clus - 2) * cb;
    uint8_t head[512], tail[512];
    if (mediaIO(m, me, false, off, head, 512) != kIOReturnSuccess ||
        mediaIO(m, me, false, off + size - 512, tail, 512) != kIOReturnSuccess) {
        snprintf(why, whySz, "rd file fail");
        return false;
    }
    if (memcmp(head, "ITLLOG01", 8)) { snprintf(why, whySz, "head magic missing (clus %llu)", clus); return false; }
    if (memcmp(tail, "ITLEND01", 8)) { snprintf(why, whySz, "tail magic missing: file not contiguous?"); return false; }
    *fileOff = off;
    *fileSize = size;
    snprintf(why, whySz, "ok clus=%llu off=%llu size=%llu", clus, off, size);
    return true;
}

static void tryAttach(IOService *me)
{
    if (logMedia) return;
    IORegistryIterator *it = IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively);
    if (!it) return;
    int seen = 0;
    char last[100] = "no candidate media";
    IORegistryEntry *e;
    while ((e = it->getNextObject())) {
        IOMedia *m = OSDynamicCast(IOMedia, e);
        if (!m || m->isWhole()) continue;
        if (m->getSize() < (64ull << 20)) continue;       // skip tiny stuff
        seen++;
        char why[100];
        UInt64 off = 0, size = 0;
        if (!m->open(me, 0, kIOStorageAccessReader)) { snprintf(last, sizeof(last), "%s: open(r) fail", e->getName()); continue; }
        bool ok = probeMedia(m, me, &off, &size, why, sizeof(why));
        m->close(me, 0);
        if (!ok) { snprintf(last, sizeof(last), "%s: %s", e->getName(), why); continue; }
        // found our file: reopen read/write
        bool rw = m->open(me, 0, kIOStorageAccessReaderWriter);
        if (!rw) rw = m->open(me, 0, kIOStorageAccessReaderWriter | kIOStorageAccessSharedLock);
        if (!rw) { snprintf(last, sizeof(last), "%s: %s but open(rw) fail", e->getName(), why); continue; }
        m->retain();
        logMedia = m;
        logClient = me;
        logStart = off + 512;
        logCap = size - 1024;
        snprintf(attachMsg, sizeof(attachMsg), "ATTACHED %s: %s", e->getName(), why);
        it->release();
        return;
    }
    it->release();
    snprintf(attachMsg, sizeof(attachMsg), "media seen=%d, last: %s", seen, last);
}

static void flushLog()
{
    if (!logMedia || !txt) return;
    size_t secs = (tlen + 511) / 512;
    if (secs * 512 > logCap) secs = (size_t)(logCap / 512);
    if (secs <= flushedSec) return;
    size_t from = flushedSec;
    while (from < secs) {
        size_t n = secs - from;
        if (n > 64) n = 64;
        uint8_t *buf = (uint8_t *)IOMalloc(n * 512);
        if (!buf) return;
        bzero(buf, n * 512);
        size_t src = from * 512;
        size_t avail = tlen > src ? tlen - src : 0;
        memcpy(buf, txt + src, avail < n * 512 ? avail : n * 512);
        IOReturn r = mediaIO(logMedia, logClient, true, logStart + from * 512, buf, n * 512);
        IOFree(buf, n * 512);
        if (r != kIOReturnSuccess) { IOLog("ItlDiag: usb log write failed 0x%x\n", r); return; }
        from += n;
    }
    // the last sector may still grow: rewrite it next time
    flushedSec = secs > 0 ? secs - 1 : 0;
}

// ---------------------------------------------------------------- clock + registry collection

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

#define MAXL 60
#define LW 112
static char lines[MAXL][LW];
static int nl;

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

    // Clock sanity: three independent time sources over the same interval.
    uint64_t dCal = (calUs() - cal0us) / 1000;
    uint64_t dAbs = (absNs() - abs0ns) / 1000000ull;
    uint64_t dTsc = rdtsc_() - tsc0;
    add("clk cal=%llums mach=%llums tsc=%lluMHz", dCal, dAbs, dCal ? dTsc / (dCal * 1000ull) : 0ull);
    add("usblog: %s", attachMsg);

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
                    const char *m = str(ch, "m0-log");
                    size_t n = strlen(m);
                    for (size_t o = 0; o < n && o < 240; o += 78) {
                        char seg[80];
                        strlcpy(seg, m + o, sizeof(seg));
                        add("  m0: %s", seg);
                    }
                    ch->release();
                }
            } else if ((c >> 8) == 0x0c03) {
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

// File-only: the complete service-plane tree, with full properties for the interesting nodes.
static void dumpTree()
{
    fileLine("---- registry tree ----");
    IORegistryIterator *it = IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively);
    if (!it) return;
    IORegistryEntry *e;
    while ((e = it->getNextObject())) {
        const char *cls = e->getMetaClass()->getClassName();
        unsigned d = e->getDepth(gIOServicePlane);
        char ind[40];
        unsigned k = d < 38 ? d : 38;
        memset(ind, ' ', k);
        ind[k] = 0;
        fileLine("%s%s <%s>", ind, e->getName(), cls);
        bool deep = !strcmp(cls, "IOUSBHostDevice") || hasStr(cls, "XHCI") || hasStr(cls, "HIDDevice") ||
                    hasStr(cls, "RNDIS") || !strcmp(cls, "itlmt7925") || hasStr(cls, "HIDKeyboard");
        if (deep) {
            OSSerialize *s = OSSerialize::withCapacity(4096);
            if (s) {
                if (e->serializeProperties(s)) {
                    const char *t = s->text();
                    if (t) { rawAppend(t, strlen(t)); rawAppend("\n", 1); }
                }
                s->release();
            }
        }
    }
    it->release();
    fileLine("---- end tree ----");
}

bool ItlDiag::start(IOService *provider)
{
    if (!super::start(provider)) return false;

    uint32_t hold = 0;
    if (!PE_parse_boot_argn("itldiag", &hold, sizeof(hold)) || !hold) return true;

    txt = (char *)IOMalloc(TXT_MAX);
    if (txt) bzero(txt, TXT_MAX);

    tsc0 = rdtsc_();
    abs0ns = absNs();
    cal0us = calUs();

    IOLog("ItlDiag: armed, waiting 10s for PCI/USB enumeration\n");
    fileLine("ItlDiag log, hold=%u", hold);
    IOSleep(10000);
    int n = 0;
    for (uint32_t t = 0; t < hold; t += 15, n++) {
        if (!logMedia) tryAttach(this);
        collect();
        outLine("ItlDiag: ===== dump at +%us, %d lines =====", 10 + t, nl);
        for (int i = 0; i < nl; i++) outLine("%s", lines[i]);
        outLine("ItlDiag: ===== end =====");
        if (n == 1 || t + 15 >= hold) dumpTree();
        flushLog();
        IOSleep(15000);
    }
    return true;
}
