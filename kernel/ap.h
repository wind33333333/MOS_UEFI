#ifndef __SMP_INIT__
#define __SMP_INIT__
#include "printk.h"
#include "lib.h"
#include "cpu.h"

void apInit(unsigned int cpuId,unsigned char bspFlags);

__attribute__((section(".init.data"))) unsigned int cpuInit_num = 0;

#endif