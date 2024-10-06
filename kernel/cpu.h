#ifndef _CPU_H
#define _CPU_H

#include "printk.h"
#include "lib.h"

void cpuInit(unsigned int *cpuId,unsigned char *bspFlags);

struct {
    char manufacturer_name[13];
    char model_name[49];
    unsigned int fundamental_frequency;
    unsigned int maximum_frequency;
    unsigned int cores_num;
    unsigned int bus_frequency;
    unsigned int tsc_frequency;
}cpu_info;


#endif