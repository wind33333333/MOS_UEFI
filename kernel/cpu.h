#ifndef _CPU_H
#define _CPU_H
#include "moslib.h"

void init_cpu_mode(void);
void get_cpu_info(void);

typedef struct {
    CHAR8 manufacturer_name[13];
    CHAR8 model_name[49];
    UINT32 fundamental_frequency;
    UINT32 maximum_frequency;
    UINT32 cores_number;
    UINT32 bus_frequency;
    UINT32 tsc_frequency;
}cpu_info_t;

extern cpu_info_t cpu_info;


#endif