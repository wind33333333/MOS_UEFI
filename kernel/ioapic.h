#ifndef __IOAPIC_H__
#define __IOAPIC_H__
#include "lib.h"
#include "cpu.h"
#include "hpet.h"

void ioapicInit(unsigned char bspFlags);

unsigned int *ioapic_baseaddr= 0;

#endif