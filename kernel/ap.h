#ifndef __SMP_INIT__
#define __SMP_INIT__
#include "moslib.h"

void init_ap(UINT32 cpu_id,UINT8 bsp_flags);

extern UINT32 init_cpu_num;

#endif