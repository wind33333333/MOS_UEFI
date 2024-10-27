#ifndef __IOAPIC_H__
#define __IOAPIC_H__
#include "moslib.h"

void init_ioapic(UINT8 bsp_flags);
extern UINT32 *ioapic_baseaddr;

#endif