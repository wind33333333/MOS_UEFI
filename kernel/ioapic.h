#ifndef __IOAPIC_H__
#define __IOAPIC_H__
#include "moslib.h"
#include "cpu.h"
#include "hpet.h"

void init_ipapic(UINT8 bsp_flags);

UINT32 *ioapic_baseaddr= 0;

#endif