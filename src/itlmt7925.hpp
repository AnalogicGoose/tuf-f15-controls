#ifndef ITLMT7925_HPP
#define ITLMT7925_HPP

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>

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
};

#endif
