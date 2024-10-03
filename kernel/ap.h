#ifndef __SMP_INIT__
#define __SMP_INIT__
#include "printk.h"
#include "lib.h"
#include "cpu.h"

void ap_init(unsigned int cpu_id,unsigned char bsp_flags);

__attribute__((section(".init.data"))) unsigned int cpu_init_num = 0;

#endif