#ifndef ITLMT7925_HPP
#define ITLMT7925_HPP

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

struct itl_dma_buf {
    IOBufferMemoryDescriptor *md = nullptr;
    uint8_t *va = nullptr;
    uint64_t pa = 0;
    size_t size = 0;
};

struct itl_ring {
    itl_dma_buf desc;      // n * 16 bytes
    itl_dma_buf buf;       // tx: one 4 KiB staging buffer, rx: n * 2048
    uint32_t regs = 0;     // BAR0 offset of this queue's register block
    int n = 0;
    int head = 0;          // tx: next slot to write; rx: next slot to refill
    int tail = 0;          // rx: next slot to read
    uint16_t slotBuf[256]; // rx: which buffer sits in which slot
};

class itlmt7925 : public IOService {
    OSDeclareDefaultStructors(itlmt7925)

public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;

private:
    IOPCIDevice *pci = nullptr;
    IOMemoryMap *bar0 = nullptr;
    volatile uint8_t *regs = nullptr;
    uint32_t barSize = 0;

    uint32_t rd(uint32_t off);
    void wr(uint32_t off, uint32_t val);
    uint32_t rdMapped(uint32_t addr);       // translates a few CONN addresses
    // M1 (gated by boot-arg itlmt_m1=<delay seconds>)
    uint32_t mapAddr(uint32_t addr, uint32_t *restoreL1);
    uint32_t rdA(uint32_t addr);
    void wrA(uint32_t addr, uint32_t val);
    void rmwA(uint32_t addr, uint32_t clr, uint32_t set);
    bool pollA(uint32_t addr, uint32_t mask, uint32_t val, uint32_t timeoutMs);
    void m1log(const char *fmt, ...);
    bool dmaAlloc(itl_dma_buf &b, size_t size);
    void dmaFree(itl_dma_buf &b);
    bool ringInit(itl_ring &r, int qid, uint32_t base, int n, bool rx);
    void ringFree(itl_ring &r);
    void dmaDisable();
    void dmaEnable();
    bool txKick(itl_ring &r, size_t len);
    int  mcuSend(uint8_t cid, const void *payload, size_t plen, bool wantResp, int *ret);
    int  fwScatter(const uint8_t *data, size_t len);
    int  rxWait(uint8_t seq, int *ret, uint32_t timeoutMs);
    int  initDownload(uint32_t addr, uint32_t len, uint32_t mode);
    int  loadPatch();
    int  loadRam();
    int  m1Run();
    void m1Main();
    static void m1Thread(void *arg);
    itl_ring txWm, txFwdl, rxEvt;
    uint8_t mcuSeq = 0;
    char m1buf[3072] = {0};
    size_t m1len = 0;
    bool m1Started = false;
    bool driverOwn();
    void dumpState();
    void holdLog();
    void nvramLog(const char *msg);       // survives reboot, readable from Windows
    char logbuf[240] = {0};
    size_t loglen = 0;
    void note(const char *fmt, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0);
};

#endif
