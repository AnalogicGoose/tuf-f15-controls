#ifndef ITLMT7925_HPP
#define ITLMT7925_HPP

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>

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
    bool driverOwn();
    void dumpState();
    void holdLog();
    void nvramLog(const char *msg);       // survives reboot, readable from Windows
    char logbuf[240] = {0};
    size_t loglen = 0;
    void note(const char *fmt, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0);
};

#endif
