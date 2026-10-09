// Register constants for M0, taken from the Linux mt76 driver
// (drivers/net/wireless/mediatek/mt76/mt792x_regs.h, mt7925/regs.h, mt7925/pci.c),
// BSD-3-Clause-Clear, Copyright (C) MediaTek Inc.
#ifndef MT7925_REGS_H
#define MT7925_REGS_H

#include <stdint.h>

#define MT_PCI_VENDOR_ID        0x14c3
#define MT_PCI_DEVICE_MT7925    0x7925

#define MT_HW_CHIPID            0x70010200u
#define MT_HW_REV               0x70010204u

#define MT_CONN_ON_LPCTL        0x7c060010u
#define PCIE_LPCR_HOST_SET_OWN  (1u << 0)
#define PCIE_LPCR_HOST_CLR_OWN  (1u << 1)
#define PCIE_LPCR_HOST_OWN_SYNC (1u << 2)

// L1 remap window used for addresses outside the fixed map
#define MT_HIF_REMAP_L1         0x155024u
#define MT_HIF_REMAP_L1_MASK    0xffff0000u
#define MT_HIF_REMAP_BASE_L1    0x130000u

// BAR0 is 2 MiB; offsets below this are direct
#define MT_BAR0_DIRECT_LIMIT    0x200000u

#endif
