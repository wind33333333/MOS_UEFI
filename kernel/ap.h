#ifndef __SMP_INIT__
#define __SMP_INIT__
#include "printk.h"
#include "moslib.h"
#include "cpu.h"

void init_ap(UINT32 cpu_id,UINT8 bsp_flags);

__attribute__((section(".init.data"))) UINT32 init_cpu_num = 0;

#endif