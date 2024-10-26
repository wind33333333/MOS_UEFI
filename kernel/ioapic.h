#ifndef __IOAPIC_H__
#define __IOAPIC_H__
#include "moslib.h"

void init_ipapic(UINT8 bsp_flags);

UINT32 *ioapic_baseaddr;

#endif