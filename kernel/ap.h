#ifndef __SMP_INIT__
#define __SMP_INIT__
#include "printk.h"
#include "moslib.h"
#include "cpu.h"

void apInit(UINT32 cpuId,UINT8 bspFlags);

__attribute__((section(".init.data"))) UINT32 cpuInit_num = 0;

#endif