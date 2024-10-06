#ifndef __IOAPIC_H__
#define __IOAPIC_H__
#include "moslib.h"
#include "cpu.h"
#include "hpet.h"

void ioapicInit(UINT8 bspFlags);

UINT32 *ioapic_baseaddr= 0;

#endif