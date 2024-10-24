#ifndef _CPU_H
#define _CPU_H
#include "moslib.h"

void init_cpu(UINT32 *cpu_id,UINT8 *bsp_flags);

typedef struct {
    CHAR8 manufacturer_name[13];
    CHAR8 model_name[49];
    UINT32 fundamental_frequency;
    UINT32 maximum_frequency;
    UINT32 cores_number;
    UINT32 bus_frequency;
    UINT32 tsc_frequency;
}cpu_info_t;

cpu_info_t cpu_info={0};

#endif