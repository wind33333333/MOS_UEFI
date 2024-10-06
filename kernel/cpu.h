#ifndef _CPU_H
#define _CPU_H

#include "printk.h"
#include "moslib.h"

void cpuInit(UINT32 *cpuId,UINT8 *bspFlags);

struct {
    CHAR8 manufacturer_name[13];
    CHAR8 model_name[49];
    UINT32 fundamental_frequency;
    UINT32 maximum_frequency;
    UINT32 cores_num;
    UINT32 bus_frequency;
    UINT32 tsc_frequency;
}cpu_info;


#endif