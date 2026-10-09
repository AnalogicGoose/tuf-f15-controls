// itlmt7925 - M0: probe, map BAR0, take driver ownership, read chip ID. Read-mostly, no DMA.
#include "itlmt7925.hpp"
#include "mt7925_regs.h"

#include <string.h>

#define super IOService
OSDefineMetaClassAndStructors(itlmt7925, IOService)

uint32_t itlmt7925::rd(uint32_t off)
{
    if (!regs || off + 4 > barSize) return 0xffffffffu;
    return *(volatile uint32_t *)(regs + off);   // x86 is little endian
}

void itlmt7925::wr(uint32_t off, uint32_t val)
{
    if (!regs || off + 4 > barSize) return;
    *(volatile uint32_t *)(regs + off) = val;
}

// Only the addresses M0 needs. The full table is in mt7925/pci.c (__mt7925_reg_addr).
uint32_t itlmt7925::rdMapped(uint32_t addr)
{
    if (addr < MT_BAR0_DIRECT_LIMIT)
        return rd(addr);

    // {0x7c060000, 0x0e0000, 0x10000}: CONN_INFRA host CSR (LPCTL lives here)
    if (addr >= 0x7c060000u && addr < 0x7c070000u)
        return rd(0x0e0000u + (addr - 0x7c060000u));

    // 0x70000000..0x78000000 is reached through the L1 remap window
    if (addr >= 0x70000000u && addr < 0x78000000u) {
        uint32_t base = addr >> 16;
        uint32_t off = addr & 0xffffu;
        uint32_t cur = rd(MT_HIF_REMAP_L1);
        wr(MT_HIF_REMAP_L1, (cur & ~MT_HIF_REMAP_L1_MASK) | (base << 16));
        (void)rd(MT_HIF_REMAP_L1);          // read back to push the write
        uint32_t v = rd(MT_HIF_REMAP_BASE_L1 + off);
        wr(MT_HIF_REMAP_L1, cur);           // restore
        return v;
    }
    return 0xffffffffu;
}

bool itlmt7925::driverOwn()
{
    // Matches __mt792xe_mcu_drv_pmctrl: write CLR_OWN, poll OWN_SYNC == 0
    const uint32_t lpctl = 0x0e0000u + (MT_CONN_ON_LPCTL - 0x7c060000u);
    for (int retry = 0; retry < 10; retry++) {
        wr(lpctl, PCIE_LPCR_HOST_CLR_OWN);
        for (int ms = 0; ms < 50; ms++) {
            if (!(rd(lpctl) & PCIE_LPCR_HOST_OWN_SYNC))
                return true;
            IODelay(1000);
        }
    }
    return false;
}

// Append a short message to the log buffer, print it, and mirror it to NVRAM variable
// 7C436110-AB2A-4BBB-A880-FE41995C9F82:itlmt7925-log (no keyboard/terminal needed to read it).
void itlmt7925::note(const char *fmt, uint32_t a, uint32_t b, uint32_t c)
{
    char tmp[80];
    snprintf(tmp, sizeof(tmp), fmt, a, b, c);
    IOLog("itlmt7925: %s\n", tmp);
    size_t n = strlen(tmp);
    if (loglen + n + 2 < sizeof(logbuf)) {
        memcpy(logbuf + loglen, tmp, n);
        loglen += n;
        logbuf[loglen++] = ';';
        logbuf[loglen] = 0;
    }
    nvramLog(logbuf);
}

void itlmt7925::nvramLog(const char *msg)
{
    IORegistryEntry *opt = IORegistryEntry::fromPath("/options", gIODTPlane);
    if (!opt) { IOLog("itlmt7925: no /options node\n"); return; }
    bool ok = opt->setProperty("itlmt7925-log", msg);
    if (!ok) IOLog("itlmt7925: NVRAM setProperty failed\n");
    opt->release();
}

void itlmt7925::dumpState()
{
    uint32_t chip = rdMapped(MT_HW_CHIPID);
    uint32_t rev  = rdMapped(MT_HW_REV);
    uint32_t lp   = rd(0x0e0000u + (MT_CONN_ON_LPCTL - 0x7c060000u));
    note("CHIP=%08x REV=%08x LP=%08x", chip, rev, lp);
    IOLog("itlmt7925: ASIC revision: %04x%02x (expect 7925xx)\n", chip & 0xffff, rev & 0xff);
}

bool itlmt7925::start(IOService *provider)
{
    if (!super::start(provider)) return false;

    pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci) { IOLog("itlmt7925: provider is not IOPCIDevice\n"); return false; }
    pci->retain();
    if (!pci->open(this)) { IOLog("itlmt7925: open failed\n"); pci->release(); pci = nullptr; return false; }

    uint16_t vid = pci->configRead16(kIOPCIConfigVendorID);
    uint16_t did = pci->configRead16(kIOPCIConfigDeviceID);
    uint16_t cmd = pci->configRead16(kIOPCIConfigCommand);
    note("start %04x:%04x cmd=%04x", vid, did, cmd);

    pci->setMemoryEnable(true);
    pci->setBusMasterEnable(false);   // M0 does no DMA

    bar0 = pci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (!bar0) { IOLog("itlmt7925: BAR0 map failed\n"); goto fail; }
    regs = (volatile uint8_t *)bar0->getVirtualAddress();
    barSize = (uint32_t)bar0->getLength();
    note("bar0 size=%x", barSize);
    if (barSize < MT_BAR0_DIRECT_LIMIT) { IOLog("itlmt7925: BAR0 too small\n"); goto fail; }

    note("lp0=%08x", rd(0x0e0000u + (MT_CONN_ON_LPCTL - 0x7c060000u)));
    if (!driverOwn())
        note("own FAIL");
    else
        note("own OK");
    dumpState();

    registerService();
    return true;

fail:
    stop(provider);
    return false;
}

void itlmt7925::stop(IOService *provider)
{
    if (bar0) { bar0->release(); bar0 = nullptr; regs = nullptr; }
    if (pci) { pci->close(this); pci->release(); pci = nullptr; }
    super::stop(provider);
}

void itlmt7925::free()
{
    super::free();
}
