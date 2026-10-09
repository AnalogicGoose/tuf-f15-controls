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
    itl_ring txWm, txFwdl, rxEvt, rxData;
    // M2 (boot-arg itlmt_m2=1): UNI commands, interface setup, scan, beacon parsing
    bool m2On = false;
    int  uniSend(uint8_t cid, const void *payload, size_t plen, bool query, uint8_t *evOut, size_t evMax,
                 uint32_t *evLen, uint32_t timeoutMs);
    bool ringPeek(itl_ring &r, const uint8_t **data, uint32_t *len, int *bi);
    void ringRelease(itl_ring &r, int bi);
    void rxDataPoll();
    void rxParseFrame(const uint8_t *p, uint32_t len);
    void rxFrameDump(const uint8_t *p, uint32_t len);
    void m2log(const char *fmt, ...);
    void m2State(const char *tag);
    uint32_t fwLogCnt = 0;
    void m2Publish();
    int  m2Run();
    int  m2Scan(const char *ssid);
    uint8_t scanSeq = 0;
    uint32_t scanMs = 0, evLogged = 0;
    uint8_t chHist[200] = {0};
    char m2buf[2048] = {0};
    size_t m2len = 0;
    char m2res[400] = {0};
    uint8_t mac[6] = {0};
    bool scanDone = false;
    uint32_t rxPkts = 0, rxBeacons = 0, rxOther = 0, rxDump = 0;
    uint32_t evCount = 0;
    struct itl_ap { uint8_t bssid[6]; uint8_t ch; char ssid[33]; } aps[48];
    int nAps = 0;
    uint8_t mcuSeq = 0;
    // M3 (boot-arg itlmt_m3=1, implies M2): associate to the configured network, WPA2-PSK, Ethernet interface
    bool m3On = false;
    itl_ring txData;                         // band0 data TX ring (mgmt + data frames)
    void m3log(const char *fmt, ...);
    void m3Publish(const char *stage);
    int  m3Run();
    void m3Capture(const uint8_t *frame, uint32_t len, const char *ssid, uint8_t ch);
    int  m3Cmd(uint8_t cid, const void *payload, size_t plen, const char *name);
    int  m3HwPrep();
    int  m3Bss(bool enable);
    int  m3Sta(uint8_t state, bool newly, uint16_t aid);
    int  m3Roc(bool acquire);
    void m3Pump(uint32_t ms);
    void m3Event(const uint8_t *ev, uint32_t len);
    void m3ParseAp();
    volatile bool rocGrant = false;
    uint8_t rocTok = 0, rocStatus = 0xff;
    uint8_t m3cipher = 0;                    // 0 = open, 4 = CCMP (BSS_INFO sec TLV)
    uint8_t apSupp = 0, apBasic = 0;         // 5 GHz legacy rate bitmaps (6,9,12,18,24,36,48,54 -> bit 0..7)
    uint8_t apRsn[64] = {0};
    uint8_t apRsnLen = 0;
    char m3buf[1700] = {0};
    size_t m3len = 0;
    char m3res[400] = {0};
    char m3ssid[33] = {0};
    char m3psk[72] = {0};
    // target AP, captured from a beacon / probe response during the scan
    bool tgtFound = false;
    uint8_t tgtBssid[6] = {0};
    uint8_t tgtCh = 0;
    uint16_t tgtCap = 0, tgtBcnInt = 0;
    uint8_t tgtIe[512] = {0};
    uint32_t tgtIeLen = 0;
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
