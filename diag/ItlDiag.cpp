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
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/storage/IOMedia.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include <stdarg.h>
#include <string.h>
#include <libkern/libkern.h>
#include <libkern/OSAtomic.h>

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
static IOLock *txtLock;              // kbWorker and worker both append

static void rawAppend(const char *s, size_t n)
{
    if (!txt) return;
    if (txtLock) IOLockLock(txtLock);
    if (tlen + n + 1 >= TXT_MAX) {
        if (!tfull) { tfull = true; const char *m = "\n[buffer full]\n"; size_t k = strlen(m); memcpy(txt + tlen, m, k); tlen += k; }
        if (txtLock) IOLockUnlock(txtLock);
        return;
    }
    memcpy(txt + tlen, s, n);
    tlen += n;
    if (txtLock) IOLockUnlock(txtLock);
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

static bool fileOut;                 // boot-arg itlfile=1: also try the (never working so far, possibly blocking) USB write paths
static void tryAttach(IOService *me)
{
    if (!fileOut) return;
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
        const char *how = "rw";
        if (!rw) {
            // A mounted read/write volume already holds the media open for writing; IOMedia::write only needs *some*
            // opener with write access, so keep a reader open and try the write anyway.
            rw = m->open(me, 0, kIOStorageAccessReader);
            how = "ro+try";
        }
        if (!rw) { snprintf(last, sizeof(last), "%s: %s but open fail", e->getName(), why); continue; }
        m->retain();
        logMedia = m;
        logClient = me;
        logStart = off + 512;
        logCap = size - 1024;
        snprintf(attachMsg, sizeof(attachMsg), "ATTACHED(%s) %s: %s", how, e->getName(), why);
        it->release();
        return;
    }
    it->release();
    snprintf(attachMsg, sizeof(attachMsg), "media seen=%d, last: %s", seen, last);
}

// ---------------------------------------------------------------- plain file write through the mounted volume
// Recovery's DiskArbitration mounts the stick as /Volumes/<label>. Writing through the filesystem needs no FAT parsing and
// no exclusive IOMedia open, so it works where the raw path cannot. The whole text is rewritten each time.

#define VFS_PATH "/Volumes/1401/ITLLOG.TXT"
static bool vfsOk;
static char vfsMsg[100] = "not tried";

// The VFS (root vnode, namei locks) is only usable once launchd runs; calling vnode_open from a kext start() thread at
// 0.7 s uptime panicked in namei(). pid 1 existing is the guard.
static bool bsdReady()
{
    proc_t p = proc_find(1);
    if (!p) return false;
    proc_rele(p);
    return true;
}

static void vfsFlush()
{
    if (!fileOut) return;
    if (!txt) return;
    if (!bsdReady()) { snprintf(vfsMsg, sizeof(vfsMsg), "waiting for launchd"); return; }
    vfs_context_t ctx = vfs_context_current();
    vnode_t vp = NULLVP;
    errno_t e = vnode_open(VFS_PATH, O_CREAT | O_TRUNC | FWRITE, 0644, 0, &vp, ctx);
    if (e) { vfsOk = false; snprintf(vfsMsg, sizeof(vfsMsg), "vnode_open %s err %d", VFS_PATH, (int)e); return; }
    int resid = 0;
    int len = (int)tlen;
    e = vn_rdwr(UIO_WRITE, vp, (caddr_t)txt, len, 0, UIO_SYSSPACE, IO_SYNC | IO_NOCACHE, vfs_context_ucred(ctx), &resid, vfs_context_proc(ctx));
    vnode_close(vp, FWRITE, ctx);
    vfsOk = (e == 0 && resid == 0);
    snprintf(vfsMsg, sizeof(vfsMsg), "%s: write %d bytes err %d resid %d", VFS_PATH, len, (int)e, resid);
}

static void flushLog()
{
    if (!fileOut) return;
    vfsFlush();
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
    add("vfs: %s", vfsMsg);

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

// ---------------------------------------------------------------- timer probe + optional key-repeat fallback
//
// Root cause of the "ddddmmmm" key repeat is not here: OpenCore (ScanAmdProcessor, family 1Ah) hands the kernel an FSB of
// 9.6 MHz instead of ~100 MHz, so every timer fires ~10x early (see oc/fix-zen5-fsb.diff). The probe below logs whether
// timers behave, so the fix can be verified with `dmesg | grep ItlDiag` or `sysctl hw.busfrequency`.
//
// The key-repeat override is only a fallback, off by default: boot-arg itlkbd=<ms> pushes the initial repeat delay to that
// many milliseconds (WindowServer/hidd set their own values, so it is re-applied every 2 s).

static int applyRepeat(uint64_t initNs, uint64_t repNs, bool verbose)
{
    OSNumber *ni = OSNumber::withNumber(initNs, 64);
    OSNumber *nr = OSNumber::withNumber(repNs, 64);
    OSDictionary *d = OSDictionary::withCapacity(2);
    if (!ni || !nr || !d) { if (ni) ni->release(); if (nr) nr->release(); if (d) d->release(); return 0; }
    d->setObject("HIDInitialKeyRepeat", ni);
    d->setObject("HIDKeyRepeat", nr);

    static const char *classes[3] = { "IOHIDSystem", "IOHIKeyboard", "IOHIDEventService" };
    int counts[3] = { 0, 0, 0 };
    for (int i = 0; i < 3; i++) {
        OSDictionary *m = IOService::serviceMatching(classes[i]);
        if (!m) continue;
        OSIterator *it = IOService::getMatchingServices(m);
        m->release();
        if (!it) continue;
        OSObject *o;
        while ((o = it->getNextObject())) {
            IOService *svc = OSDynamicCast(IOService, o);
            if (!svc) continue;
            counts[i]++;
            if (i < 2) {
                IOReturn r = svc->setProperties(d);
                if (verbose) IOLog("ItlDiag: kbd setProperties(%s %s) -> 0x%x\n", classes[i], svc->getName(), r);
            }
            if (i > 0) {
                svc->setProperty("HIDInitialKeyRepeat", ni);
                svc->setProperty("HIDKeyRepeat", nr);
            }
        }
        it->release();
    }
    if (verbose)
        IOLog("ItlDiag: kbd hidsystem=%d ihikeyboard=%d hideventservice=%d (initial repeat %llu ms)\n", counts[0], counts[1],
              counts[2], initNs / 1000000ull);
    d->release(); ni->release(); nr->release();
    return counts[0] + counts[1] + counts[2];
}

// Timer sanity, printed to the kernel log so `dmesg | grep ItlDiag` shows it: does IOSleep(100) take 100 ms of wall clock?
static char lastProbe[100] = "probe: -";
static void timerProbe(void)
{
    uint64_t a0 = absNs(), c0 = calUs(), t0 = rdtsc_();
    IOSleep(100);
    uint64_t a1 = absNs(), c1 = calUs(), t1 = rdtsc_();
    uint64_t dc = (c1 - c0) / 1000;
    outLine("ItlDiag: probe sleep100: mach=%llums cal=%llums tsc=%lluMHz", (a1 - a0) / 1000000ull, dc, dc ? (t1 - t0) / (dc * 1000ull) : 0ull);
    snprintf(lastProbe, sizeof(lastProbe), "probe sleep100: mach=%llums cal=%llums tsc=%lluMHz", (a1 - a0) / 1000000ull, dc, dc ? (t1 - t0) / (dc * 1000ull) : 0ull);
}

static void kbWorker(void *)
{
    uint32_t initMs = 0;
    PE_parse_boot_argn("itlkbd", &initMs, sizeof(initMs));        // fallback only: initial repeat delay in ms, 0 = off
    uint64_t initNs = (uint64_t)initMs * 1000000ull, repNs = 120ull * 1000000ull;
    IOLog("ItlDiag: timers: kbd fallback %s (itlkbd=%u)\n", initMs ? "ON" : "off", initMs);
    for (int pass = 0; pass < 600; pass++) {                       // ~20 minutes
        if (initMs) applyRepeat(initNs, repNs, pass < 3 || (pass % 30) == 0);
        if (pass < 5 || pass % 15 == 0) timerProbe();
        IOSleep(2000);
    }
}

// itlmt7925 keeps its stage log in the registry ("m0-log" at probe, "m1-log" for the firmware download); copy any that
// grew into the file so the M1 result is on the stick even if nothing could be typed in Recovery.
static void copyKextLogs()
{
    static size_t seen0, seen1;
    IORegistryIterator *it = IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively);
    if (!it) return;
    IORegistryEntry *e;
    while ((e = it->getNextObject())) {
        static const char *keys[2] = { "m0-log", "m1-log" };
        size_t *seen[2] = { &seen0, &seen1 };
        for (int k = 0; k < 2; k++) {
            OSString *v = OSDynamicCast(OSString, e->getProperty(keys[k]));
            if (!v) continue;
            const char *m = v->getCStringNoCopy();
            size_t n = strlen(m);
            if (n == *seen[k]) continue;
            *seen[k] = n;
            fileLine("---- %s (%lu bytes) ----", keys[k], (unsigned long)n);
            for (size_t o = 0; o < n; o += 100) {
                char seg[104];
                strlcpy(seg, m + o, 101);
                fileLine("%s", seg);
            }
        }
    }
    it->release();
}

// ---------------------------------------------------------------- xHCI interrupt health
// Hypothesis under test: macOS never services some xHCI events (the key-up report sits in the event ring until the next
// interrupt), so keys repeat until the next key. A sampler reads USBSTS.EINT / IMAN.IP of every xHCI every few ms: with
// healthy interrupts those bits pulse for microseconds; if interrupts are lost they stay set for a long time.

struct Xhc {
    IOPCIDevice *p;
    IOMemoryMap *map;
    volatile uint8_t *b;
    uint32_t op, rt;
    unsigned vid, did;
    uint32_t lastCmd, samples, rises, maxMs, slow, curMs, erdpCh, dead, lastSts, lastIman, lastErdp;
    uint64_t riseNs;
    bool up;
};
static Xhc xh[8];
static int nxh;
static inline uint32_t rd32(const Xhc &x, uint32_t o) { return *(volatile uint32_t *)(x.b + o); }

static void xhcInit()
{
    if (nxh) return;
    IORegistryIterator *it = IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively);
    if (!it) return;
    IORegistryEntry *e;
    while ((e = it->getNextObject()) && nxh < 8) {
        IOPCIDevice *p = OSDynamicCast(IOPCIDevice, e);
        if (!p || (p->configRead32(kIOPCIConfigRevisionID) >> 8) != 0x0c0330) continue;
        Xhc &x = xh[nxh];
        bzero(&x, sizeof(x));
        x.p = p;
        p->retain();
        x.vid = p->configRead16(kIOPCIConfigVendorID);
        x.did = p->configRead16(kIOPCIConfigDeviceID);
        x.map = p->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
        if (x.map) {
            x.b = (volatile uint8_t *)x.map->getVirtualAddress();
            if (x.b) {
                x.op = *(volatile uint32_t *)x.b & 0xff;
                x.rt = *(volatile uint32_t *)(x.b + 0x18) & ~0x1fu;
                if (x.op < 0x10 || x.op > 0x100 || !x.rt || x.rt > 0x10000) x.b = nullptr;
            }
        }
        nxh++;
    }
    it->release();
}

// ---------------------------------------------------------------- xHCI event ring watcher
// Root-cause test for the key repeat bug ("key-up arrives only with the next key"). The sampler snapshots the event ring of
// each xHCI (cycle bit flips mark new TRBs) and records when each event was first visible and when the OS moved ERDP past it.
//   event visible late             -> controller / endpoint side (doorbell, TD not queued, endpoint stopped)
//   event visible, ERDP moves late -> OS did not service the interrupt (lost / delayed MSI)
// It also reads the endpoint states from the device contexts and compares MFINDEX (8 kHz hardware clock) with mach time.
struct XEv { uint32_t tms; uint8_t type, slot, dci, cc; uint32_t lat; };
struct XPend { uint32_t idx; uint64_t t; int rec; };
struct XRing {
    bool ok;
    uint64_t segPa, ersta;
    uint32_t n;                       // TRBs in segment 0
    volatile uint8_t *seg;
    uint64_t *snap;
    uint32_t nev, nxfer, late5, maxLat, ipMax, pendMax;
    uint32_t cnt[16][32];
    XEv evs[16]; uint32_t evHead;
    XPend pend[32]; int npend;
    uint64_t mfAbs0, mfAbsTot, mfTot; uint32_t mfLast; uint64_t ipRise; bool ipUp;
    uint64_t ehbRise; bool ehbUp; uint32_t ehbMax, ehbCur, erdpAdv, ehbN, ipN; uint64_t lastErdp;
    // time line of ERDP advances (= events the OS consumed), bursts within 3 ms merged: tms, events
    uint32_t advT[64], advN[64], advHead; uint64_t advLast;
};
static XRing xr[8];
static uint64_t xhcStart;
// Mapping xHCI-owned RAM (event ring, device contexts) is only done with boot-arg itlxr=1: the first boot that did it
// panicked in AppleUSBXHCIDevice::free during a port terminate. Default is registers only.
static bool physOk;

struct PM { IOMemoryDescriptor *d; IOMemoryMap *m; uint64_t pa; };
static PM pmc[32];
static int npm;
static volatile uint8_t *physMap(uint64_t pa, size_t len)
{
    uint64_t base = pa & ~0xfffULL;
    size_t need = (size_t)(pa - base) + len;
    need = (need + 0xfff) & ~(size_t)0xfff;
    for (int i = 0; i < npm; i++)
        if (pmc[i].pa == base && pmc[i].m && pmc[i].m->getLength() >= need) return (volatile uint8_t *)pmc[i].m->getVirtualAddress() + (pa - base);
    if (npm >= 32) return nullptr;
    IOMemoryDescriptor *d = IOMemoryDescriptor::withPhysicalAddress(base, need, kIODirectionInOut);
    if (!d) return nullptr;
    IOMemoryMap *m = d->map();
    if (!m) { d->release(); return nullptr; }
    pmc[npm].d = d; pmc[npm].m = m; pmc[npm].pa = base; npm++;
    return (volatile uint8_t *)m->getVirtualAddress() + (pa - base);
}

static void xrInit(int i)
{
    Xhc &x = xh[i];
    XRing &r = xr[i];
    if (r.ok || !x.b || !physOk) return;
    uint32_t szReg = rd32(x, x.rt + 0x28) & 0xffff;
    uint64_t erstba = ((uint64_t)rd32(x, x.rt + 0x34) << 32) | (rd32(x, x.rt + 0x30) & ~0x3fu);
    if (!szReg || !erstba) return;
    volatile uint8_t *erst = physMap(erstba, 16);
    if (!erst) return;
    uint64_t segPa = ((uint64_t)*(volatile uint32_t *)(erst + 4) << 32) | *(volatile uint32_t *)erst;
    uint32_t n = *(volatile uint32_t *)(erst + 8) & 0xffff;
    if (!segPa || n < 16 || n > 1024) return;
    volatile uint8_t *seg = physMap(segPa, (size_t)n * 16);
    if (!seg) return;
    r.snap = (uint64_t *)IOMalloc(n * 8);
    if (!r.snap) return;
    r.segPa = segPa; r.n = n; r.seg = seg; r.ersta = erstba;
    for (uint32_t k = 0; k < n; k++)
        r.snap[k] = *(volatile uint32_t *)(seg + k * 16 + 8) | ((uint64_t)*(volatile uint32_t *)(seg + k * 16 + 12) << 32);
    r.ok = true;
}

static void xrRecord(int i, uint32_t idx, uint64_t now, uint64_t start, uint32_t d2, uint32_t d3)
{
    XRing &r = xr[i];
    uint8_t type = (d3 >> 10) & 0x3f, slot = d3 >> 24, dci = (d3 >> 16) & 0x1f, cc = d2 >> 24;
    r.nev++;
    XEv &e = r.evs[r.evHead++ & 15];
    e.tms = (uint32_t)((now - start) / 1000000ull); e.type = type; e.slot = slot; e.dci = dci; e.cc = cc; e.lat = 0xffffffffu;
    if (type == 32) { r.nxfer++; if (slot < 16 && dci < 32) r.cnt[slot][dci]++; }
    if (r.npend < 32) {
        r.pend[r.npend].idx = idx; r.pend[r.npend].t = now; r.pend[r.npend].rec = (int)((r.evHead - 1) & 15);
        r.npend++;
    }
}

static void xrPoll(int i, uint64_t now, uint64_t start)
{
    Xhc &x = xh[i];
    XRing &r = xr[i];
    if (!r.ok) xrInit(i);
    for (uint32_t k = 0; r.ok && k < r.n; k++) {
        uint32_t d3 = *(volatile uint32_t *)(r.seg + k * 16 + 12);
        uint32_t s3 = (uint32_t)(r.snap[k] >> 32);
        if (!((d3 ^ s3) & 1)) continue;
        uint32_t d2 = *(volatile uint32_t *)(r.seg + k * 16 + 8);
        r.snap[k] = d2 | ((uint64_t)d3 << 32);
        xrRecord(i, k, now, start, d2, d3);
    }
    // which TRB will the OS handle next?
    uint32_t erdpLo = rd32(x, x.rt + 0x38);
    uint64_t erdp = ((uint64_t)rd32(x, x.rt + 0x3c) << 32) | (erdpLo & ~0xfu);
    if (erdp != r.lastErdp) {
        r.erdpAdv++; r.lastErdp = erdp;
        uint32_t tms = (uint32_t)((now - start) / 1000000ull);
        if (r.advHead && now - r.advLast < 3000000ull) r.advN[(r.advHead - 1) & 63]++;
        else { r.advT[r.advHead & 63] = tms; r.advN[r.advHead & 63] = 1; r.advHead++; }
        r.advLast = now;
    }
    // ERDP.EHB (bit 3): set by the controller when it raised the interrupt, cleared only when the OS acknowledged it
    bool ehb = erdpLo & 8;
    if (ehb && !r.ehbUp) { r.ehbUp = true; r.ehbRise = now; r.ehbN++; }
    else if (!ehb && r.ehbUp) {
        r.ehbUp = false;
        uint32_t ms = (uint32_t)((now - r.ehbRise) / 1000000ull);
        if (ms > r.ehbMax) r.ehbMax = ms;
    }
    r.ehbCur = r.ehbUp ? (uint32_t)((now - r.ehbRise) / 1000000ull) : 0;
    if (r.ehbCur > r.ehbMax) r.ehbMax = r.ehbCur;
    if (r.ok && erdp >= r.segPa && erdp < r.segPa + (uint64_t)r.n * 16) {
        uint32_t cur = (uint32_t)((erdp - r.segPa) / 16);
        for (int p = 0; p < r.npend; ) {
            uint32_t ahead = (cur + r.n - r.pend[p].idx) % r.n;     // > 0: the OS has moved past this event
            if (ahead > 0 && ahead < r.n / 2) {
                uint32_t lat = (uint32_t)((now - r.pend[p].t) / 1000000ull);
                r.evs[r.pend[p].rec].lat = lat;
                if (lat > r.maxLat) r.maxLat = lat;
                if (lat >= 5) r.late5++;
                r.pend[p] = r.pend[--r.npend];
            } else {
                uint32_t age = (uint32_t)((now - r.pend[p].t) / 1000000ull);
                if (age > r.pendMax) r.pendMax = age;
                p++;
            }
        }
    }
    // IMAN.IP pending time
    bool ip = rd32(x, x.rt + 0x20) & 1;
    if (ip && !r.ipUp) { r.ipUp = true; r.ipRise = now; r.ipN++; }
    else if (!ip && r.ipUp) {
        r.ipUp = false;
        uint32_t ms = (uint32_t)((now - r.ipRise) / 1000000ull);
        if (ms > r.ipMax) r.ipMax = ms;
    }
    if (r.ipUp) { uint32_t ms = (uint32_t)((now - r.ipRise) / 1000000ull); if (ms > r.ipMax) r.ipMax = ms; }
    // MFINDEX vs mach time, sampled every ~250 ms
    if (!r.mfAbs0) { r.mfAbs0 = now; r.mfLast = rd32(x, x.rt) & 0x3fff; }
    else if (now - r.mfAbs0 >= 250000000ull) {
        uint32_t mf = rd32(x, x.rt) & 0x3fff;
        r.mfTot += (mf - r.mfLast) & 0x3fff;
        r.mfAbsTot += now - r.mfAbs0;
        r.mfLast = mf; r.mfAbs0 = now;
    }
}

// one text line per xHCI, written to NVRAM as itlx<i>
#define ADV(...) do { o += snprintf(out + o, cap - o, __VA_ARGS__); if (o >= cap) o = cap - 1; } while (0)
static void xrReport(int i, char *out, size_t cap, uint64_t start)
{
    Xhc &x = xh[i];
    XRing &r = xr[i];
    size_t o = 0;
    uint32_t nowMs = (uint32_t)((absNs() - start) / 1000000ull);
    uint32_t mfRatio = r.mfAbsTot ? (uint32_t)((r.mfTot * 125000ull * 1000ull) / r.mfAbsTot) : 0;
    ADV("x%d T=%u ring=%d ev=%u tx=%u late5=%u maxlat=%ums pend=%u ip=%u/%ums ehb=%u/%ums(cur%u) adv=%u mf=%u/1000 |", i, nowMs,
                  r.ok ? 1 : 0, r.nev, r.nxfer, r.late5, r.maxLat, r.pendMax, r.ipN, r.ipMax, r.ehbN, r.ehbMax, r.ehbCur,
                  r.erdpAdv, mfRatio);
    if (x.b && physOk) {
        uint32_t hcc = rd32(x, 0x10);
        uint32_t csz = (hcc & 4) ? 64 : 32;
        uint64_t dcbaa = ((uint64_t)rd32(x, x.op + 0x34) << 32) | (rd32(x, x.op + 0x30) & ~0x3fu);
        volatile uint8_t *dc = dcbaa ? physMap(dcbaa, 16 * 8) : nullptr;
        if (dc) {
            for (int s = 1; s < 16 && o + 24 < cap; s++) {
                uint64_t dp = *(volatile uint64_t *)(dc + s * 8) & ~0x3fULL;
                if (!dp) continue;
                volatile uint8_t *ctx = physMap(dp, csz * 10);
                if (!ctx) continue;
                ADV(" s%d:", s);
                for (int d = 1; d <= 9; d++)
                    ADV("%u", *(volatile uint32_t *)(ctx + d * csz) & 7);
            }
        }
    }
    ADV(" | adv:");
    {
        uint32_t na = r.advHead < 64 ? r.advHead : 64;
        // newest first so the part that fits is the part around the key test
        for (uint32_t k = 0; k < na && o + 16 < cap; k++) {
            uint32_t ix = (r.advHead - 1 - k) & 63;
            ADV(" %u:%u", r.advT[ix] / 100, r.advN[ix]);
        }
    }
    ADV(" | cnt:");
    for (int s = 0; s < 16; s++)
        for (int d = 0; d < 32 && o + 14 < cap; d++)
            if (r.cnt[s][d]) ADV(" %d.%d=%u", s, d, r.cnt[s][d]);
    ADV(" | last:");
    uint32_t cntEv = r.nev < 16 ? r.nev : 16;
    for (uint32_t k = 0; k < cntEv && o + 30 < cap; k++) {
        XEv &e = r.evs[(r.evHead - cntEv + k) & 15];
        if (e.lat == 0xffffffffu) ADV(" %u:t%u/%u.%u/c%u/L-", e.tms, e.type, e.slot, e.dci, e.cc);
        else ADV(" %u:t%u/%u.%u/c%u/L%u", e.tms, e.type, e.slot, e.dci, e.cc, e.lat);
    }
}
#undef ADV

static void sampler(void *)
{
    uint64_t start = absNs();
    xhcStart = start;
    uint32_t last[8] = { 0 };
    while (absNs() - start < 900ull * 1000000000ull) {
        uint64_t now = absNs();
        for (int i = 0; i < nxh; i++) {
            Xhc &x = xh[i];
            if (!x.b) continue;
            uint32_t sts = rd32(x, x.op + 4), iman = rd32(x, x.rt + 0x20), erdp = rd32(x, x.rt + 0x38) & ~0xfu;
            x.samples++;
            x.lastSts = sts; x.lastIman = iman; x.lastCmd = rd32(x, x.op); x.lastErdp = erdp;
            if (sts == 0xffffffffu) { x.dead++; continue; }
            bool eint = (iman & 1);          // USBSTS.EINT is never cleared by AppleUSBXHCI, so only IMAN.IP says anything
            if (eint && !x.up) { x.up = true; x.riseNs = now; x.rises++; }
            else if (!eint && x.up) {
                x.up = false;
                uint32_t ms = (uint32_t)((now - x.riseNs) / 1000000ull);
                if (ms > x.maxMs) x.maxMs = ms;
                if (ms >= 20) x.slow++;
            }
            x.curMs = x.up ? (uint32_t)((now - x.riseNs) / 1000000ull) : 0;
            if (x.samples > 1 && erdp != last[i]) x.erdpCh++;
            last[i] = erdp;
            xrPoll(i, now, start);
        }
        IOSleep(1);
    }
}

static void screenBlock(int n)
{
    uint64_t dCal = (calUs() - cal0us) / 1000;
    uint64_t dAbs = (absNs() - abs0ns) / 1000000ull;
    uint64_t dTsc = rdtsc_() - tsc0;
    outLine("ItlDiag: ===== block %d =====", n);
    outLine("clk cal=%llums mach=%llums tsc=%lluMHz", dCal, dAbs, dCal ? dTsc / (dCal * 1000ull) : 0ull);
    outLine("%.98s", lastProbe);
    outLine("usblog: %.40s | vfs: %.40s", attachMsg, vfsMsg);
    for (int i = 0; i < nxh; i++) {
        Xhc &x = xh[i];
        IOPCIDevice *p = x.p;
        unsigned msi = 0xffff, msix = 0xffff;
        uint8_t ptr = p->configRead8(0x34) & ~3;
        for (int g = 0; ptr >= 0x40 && g < 32; g++) {
            uint8_t id = p->configRead8(ptr);
            if (id == 5) msi = p->configRead16(ptr + 2);
            if (id == 0x11) msix = p->configRead16(ptr + 2);
            ptr = p->configRead8(ptr + 1) & ~3;
        }
        const char *ic = "-";
        unsigned nspec = 0;
        OSArray *ca = OSDynamicCast(OSArray, p->getProperty("IOInterruptControllers"));
        if (ca && ca->getCount()) { OSString *os = OSDynamicCast(OSString, ca->getObject(0)); if (os) ic = os->getCStringNoCopy(); }
        OSArray *sa = OSDynamicCast(OSArray, p->getProperty("IOInterruptSpecifiers"));
        if (sa) nspec = sa->getCount();
        outLine("x%d %04x:%04x cmd=%04x sts=%04x il=%02x msi=%04x msix=%04x", i, x.vid, x.did, p->configRead16(kIOPCIConfigCommand),
                p->configRead16(kIOPCIConfigStatus), p->configRead8(0x3c), msi, msix);
        outLine("   drv=%.24s irq=%.36s nspec=%u", firstChildClass(p), ic, nspec);
        if (!x.b) { outLine("   mmio: not mapped"); continue; }
        outLine("   usbsts=%08x iman=%08x imod=%08x erdp=%08x", x.lastSts, x.lastIman, rd32(x, x.rt + 0x24), x.lastErdp);
        outLine("   sam=%u eintRises=%u maxEint=%ums slow=%u cur=%ums erdpChg=%u dead=%u", x.samples, x.rises, x.maxMs, x.slow,
                x.curMs, x.erdpCh, x.dead);
    }
    IORegistryIterator *it = IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively);
    if (it) {
        IORegistryEntry *e;
        int nu = 0, nh = 0;
        while ((e = it->getNextObject())) {
            const char *cls = e->getMetaClass()->getClassName();
            if (!strcmp(cls, "IOUSBHostDevice")) {
                if (nu++ < 8) outLine("usb %04x:%04x %.30s loc=%08x spd=%u", num(e, "idVendor") & 0xffff, num(e, "idProduct") & 0xffff,
                                      e->getName(), num(e, "locationID"), num(e, "Device Speed"));
            } else if (hasStr(cls, "HIDDevice") || hasStr(cls, "HIDKeyboard")) {
                nh++;
            }
        }
        it->release();
        outLine("usb devices=%d hid=%d", nu, nh);
    }
    outLine("ItlDiag: ===== end block %d =====", n);
}

// Last resort when no file write path works and the verbose output scrolls away: boot-arg itlpanic=<seconds> ends the run
// with one panic whose string is the whole digest, so the panic screen is a single static frame to photograph.
// ---------------------------------------------------------------- cross-CPU clock skew
// If the TSC is not synchronised between cores, mach_absolute_time() read on one core can be earlier than a value another core
// published a moment before. Event timestamps (key-down on one core, key-up on another) would then be out of order and the
// HID event system can drop or reorder them. Every thread publishes its time and checks it against the latest published one.
static volatile uint64_t skLast;
static volatile uint64_t skMaxBack;
static volatile uint32_t skBackAt;           // (publisher apic << 16) | reader apic of the worst case
static volatile uint32_t skSeen[4];          // bitmap of x2APIC ids that ran a skew thread
static volatile uint32_t skStop, skCount, skThreads;
static volatile uint64_t skPub;              // apic id of the last publisher

static inline uint32_t apicId()
{
    uint32_t a = 0xb, b, c = 0, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    return d;
}

static void skewThread(void *)
{
    OSIncrementAtomic((volatile SInt32 *)&skThreads);
    uint32_t me = apicId() & 0x7f;
    while (!skStop) {
        uint64_t prev = skLast;
        uint32_t who = (uint32_t)skPub;
        uint64_t t = absNs();
        me = apicId() & 0x7f;
        OSBitOrAtomic(1u << (me & 31), (volatile UInt32 *)&skSeen[(me >> 5) & 3]);
        if (t < prev) {
            OSIncrementAtomic((volatile SInt32 *)&skCount);
            if (prev - t > skMaxBack) { skMaxBack = prev - t; skBackAt = (who << 16) | me; }
        }
        while (t > prev) {
            if (OSCompareAndSwap64(prev, t, &skLast)) { skPub = me; break; }
            prev = skLast;
        }
    }
    OSDecrementAtomic((volatile SInt32 *)&skThreads);
}

static void skewTest(int nthreads, unsigned ms)
{
    skStop = 0;
    for (int i = 0; i < nthreads; i++) IOCreateThread(skewThread, nullptr);
    IOSleep(ms);
    skStop = 1;
    IOSleep(50);
}

static char digestBuf[1100];
static void buildDigest()
{
    char *b = digestBuf;
    int o = 0;
    uint64_t dCal = (calUs() - cal0us) / 1000, dAbs = (absNs() - abs0ns) / 1000000ull;
    o += snprintf(b + o, sizeof(digestBuf) - o, "ITLDIAG cal=%llums mach=%llums tsc=%lluMHz | ", dCal, dAbs,
                  dCal ? (rdtsc_() - tsc0) / (dCal * 1000ull) : 0ull);
    o += snprintf(b + o, sizeof(digestBuf) - o, "nxh=%d ", nxh);
    o += snprintf(b + o, sizeof(digestBuf) - o, "SKEW back=%lluns n=%u pub%u>rd%u cpus=%x/%x/%x/%x | ", (unsigned long long)skMaxBack,
                  (unsigned)skCount, (unsigned)(skBackAt >> 16), (unsigned)(skBackAt & 0xffff), (unsigned)skSeen[0], (unsigned)skSeen[1],
                  (unsigned)skSeen[2], (unsigned)skSeen[3]);
    for (int i = 0; i < nxh && o < 800; i++) {
        Xhc &x = xh[i];
        IOPCIDevice *p = x.p;
        unsigned msi = 0xffff, msix = 0xffff;
        uint8_t ptr = p->configRead8(0x34) & ~3;
        for (int g = 0; ptr >= 0x40 && g < 32; g++) {
            uint8_t id = p->configRead8(ptr);
            if (id == 5) msi = p->configRead16(ptr + 2);
            if (id == 0x11) msix = p->configRead16(ptr + 2);
            ptr = p->configRead8(ptr + 1) & ~3;
        }
        const char *ic = "-";
        unsigned nspec = 0;
        OSArray *ca = OSDynamicCast(OSArray, p->getProperty("IOInterruptControllers"));
        if (ca && ca->getCount()) { OSString *os = OSDynamicCast(OSString, ca->getObject(0)); if (os) ic = os->getCStringNoCopy(); }
        OSArray *sa = OSDynamicCast(OSArray, p->getProperty("IOInterruptSpecifiers"));
        if (sa) nspec = sa->getCount();
        o += snprintf(b + o, sizeof(digestBuf) - o, "X%d:%04x ucmd=%x cmd=%x msi=%x msix=%x irq=%.16s n=%u sts=%x im=%x sam=%u rise=%u max=%ums slow=%u cur=%ums erdp=%u dead=%u | ",
                      i, x.did, x.lastCmd, p->configRead16(kIOPCIConfigCommand), msi, msix, ic, nspec, x.lastSts, x.lastIman, x.samples,
                      x.rises, x.maxMs, x.slow, x.curMs, x.erdpCh, x.dead);
    }
    IORegistryIterator *it = IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively);
    if (it) {
        IORegistryEntry *e;
        int nu = 0, nh = 0;
        while ((e = it->getNextObject())) {
            const char *cls = e->getMetaClass()->getClassName();
            if (!strcmp(cls, "IOUSBHostDevice")) {
                if (nu++ < 8 && o < 1000)
                    o += snprintf(b + o, sizeof(digestBuf) - o, "U%04x:%04x/%x/s%u ", num(e, "idVendor") & 0xffff, num(e, "idProduct") & 0xffff,
                                  (unsigned)num(e, "locationID") >> 20, (unsigned)num(e, "Device Speed"));
            } else if (hasStr(cls, "HIDDevice") || hasStr(cls, "HIDKeyboard")) {
                nh++;
            }
        }
        it->release();
        o += snprintf(b + o, sizeof(digestBuf) - o, "usb=%d hid=%d", nu, nh);
    }
}

static void panicDigest()
{
    buildDigest();
    panic("%s", digestBuf);
}

// Persist the digest in firmware NVRAM (itl0..itl3, Apple GUID) so it can be read from Windows afterwards
// (scripts/read-nvram.ps1, elevated). Works even when no USB write path does.
static void nvramPut(const char *name, const char *s, size_t n)
{
    // The firmware variable store only reclaims deleted copies at boot, so rewriting 14 variables every 10 s fills it up and
    // later writes fail silently. Skip writes whose content did not change.
    static struct { char nm[8]; uint32_t h; } seen[32];
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    h ^= (uint32_t)n;
    for (int i = 0; i < 32; i++) {
        if (!seen[i].nm[0]) { strlcpy(seen[i].nm, name, sizeof(seen[i].nm)); seen[i].h = h; break; }
        if (strncmp(seen[i].nm, name, sizeof(seen[i].nm)) == 0) {
            if (seen[i].h == h) return;
            seen[i].h = h;
            break;
        }
    }
    IORegistryEntry *o = IORegistryEntry::fromPath("/options", gIODTPlane);
    if (!o) return;
    OSData *d = OSData::withBytes(s, (unsigned)n);
    if (d) { o->setProperty(name, d); d->release(); }
    o->setProperty("IONVRAM-FORCESYNCNOW-PROPERTY", name);
    o->release();
}

// itlm0 / itlm1: tail of the itlmt7925 stage logs (M0 probe, M1 firmware download), same registry properties copyKextLogs reads.
static void nvramKextLogs()
{
    IORegistryIterator *it = IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively);
    if (!it) return;
    IORegistryEntry *e;
    static const char *keys[6] = { "m0-log", "m1-log", "m2-log", "m2-res", "m3-log", "m3-res" };
    static const char *vars[6] = { "itlm0", "itlm1", "itlm2", "itlm3", "itln0", "itln4" };
    while ((e = it->getNextObject())) {
        for (int k = 0; k < 6; k++) {
            OSString *v = OSDynamicCast(OSString, e->getProperty(keys[k]));
            if (!v) continue;
            const char *m = v->getCStringNoCopy();
            size_t n = strlen(m);
            if (k == 2) {                                   // m2-log: from the start, 380 chars per variable
                static const char *more[3] = { "itlm2", "itlm4", "itlm5" };
                for (int c = 0; c < 3 && (size_t)c * 380 < n; c++) {
                    size_t f = (size_t)c * 380, t = n - f > 380 ? 380 : n - f;
                    nvramPut(more[c], m + f, t);
                }
                continue;
            }
            if (k == 4) {                                   // m3-log: from the start, 380 chars per variable, itln0..itln3
                static const char *m3v[4] = { "itln0", "itln1", "itln2", "itln3" };
                for (int c = 0; c < 4 && (size_t)c * 380 < n; c++) {
                    size_t f = (size_t)c * 380, t = n - f > 380 ? 380 : n - f;
                    nvramPut(m3v[c], m + f, t);
                }
                continue;
            }
            size_t from = (k < 3 && n > 380) ? n - 380 : 0;
            if ((k == 3 || k == 5) && n > 380) n = 380;
            nvramPut(vars[k], m + from, n - from);
        }
    }
    it->release();
}

// itlx0..itlx3: xHCI event ring report per controller (see xrReport)
static void nvramXrep()
{
    for (int i = 0; i < nxh && i < 4; i++) {
        static char buf[400];
        xrReport(i, buf, 380, xhcStart);
        char nm[8];
        snprintf(nm, sizeof(nm), "itlx%d", i);
        nvramPut(nm, buf, strlen(buf));
    }
}

static void nvramDigest(int n)
{
    nvramKextLogs();
    nvramXrep();
    buildDigest();
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "n=%d ", n);
    size_t len = strlen(digestBuf);
    char chunk[400];
    for (int i = 0; i < 4; i++) {
        size_t from = (size_t)i * 330;
        if (from >= len && i) break;
        size_t take = len > from ? (len - from > 330 ? 330 : len - from) : 0;
        snprintf(chunk, sizeof(chunk), "%s%.*s", i ? "" : hdr, (int)take, digestBuf + from);
        char nm[8];
        snprintf(nm, sizeof(nm), "itl%d", i);
        nvramPut(nm, chunk, strlen(chunk));
    }
}

// Runs on its own thread so the boot is never held up. Prints a compact summary block to the verbose console every few
// seconds (photograph the last one before the GUI covers it), mirrors everything to the stick when a write path works,
// and keeps appending (timer probes, M1 log) for ~20 minutes.
static void worker(void *arg)
{
    IOService *me = (IOService *)arg;
    uint32_t xr1 = 0;
    PE_parse_boot_argn("itlxr", &xr1, sizeof(xr1));
    physOk = xr1 != 0;
    nvramPut("itlx0", "worker up", 9);
    xhcInit();
    { char hb[64]; snprintf(hb, sizeof(hb), "xhcInit nxh=%d physOk=%d", nxh, physOk ? 1 : 0); nvramPut("itlx0", hb, strlen(hb)); }
    IOCreateThread(sampler, nullptr);
    uint32_t panicSec = 0;
    PE_parse_boot_argn("itlpanic", &panicSec, sizeof(panicSec));
    for (int n = 0; n < 12; n++) {
        IOSleep(n ? 5000 : 8000);
        if (panicSec && (absNs() - abs0ns) / 1000000000ull >= panicSec) panicDigest();
        if (!logMedia && !vfsOk) { vfsFlush(); tryAttach(me); }
        if (!nxh) xhcInit();
        if (n == 1 || n == 7) skewTest(24, 2500);
        screenBlock(n);
        nvramDigest(n);
        if (n == 2) {
            collect();
            fileLine("===== registry summary, %d lines =====", nl);
            for (int i = 0; i < nl; i++) fileLine("%s", lines[i]);
            dumpTree();
        }
        flushLog();
    }
    outLine("ItlDiag: done (usblog: %s | vfs: %s)", attachMsg, vfsMsg);
    flushLog();
    for (int pass = 0; pass < 120; pass++) {
        IOSleep(10000);
        if (panicSec && (absNs() - abs0ns) / 1000000000ull >= panicSec) panicDigest();
        if (!logMedia && !vfsOk) tryAttach(me);
        if (!nxh) xhcInit();
        screenBlock(100 + pass);
        nvramDigest(100 + pass);
        copyKextLogs();
        flushLog();
    }
}

bool ItlDiag::start(IOService *provider)
{
    if (!super::start(provider)) return false;

    uint32_t hold = 0, fo = 0;
    PE_parse_boot_argn("itlfile", &fo, sizeof(fo));
    fileOut = fo != 0;
    if (PE_parse_boot_argn("itldiag", &hold, sizeof(hold)) && hold) {
        txtLock = IOLockAlloc();
        txt = (char *)IOMalloc(TXT_MAX);
        if (txt) bzero(txt, TXT_MAX);
    }
    IOCreateThread(kbWorker, this);
    if (!hold) return true;

    tsc0 = rdtsc_();
    abs0ns = absNs();
    cal0us = calUs();
    fileLine("ItlDiag log, itldiag=%u", hold);

    IOCreateThread(worker, this);
    return true;
}
