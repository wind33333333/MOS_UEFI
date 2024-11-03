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

#define GET_APICID(APICID) \
                    do{    \
                        __asm__ __volatile__(               \
                                "movl   $0xb,%%eax  \n\t"   \
                                "xorl   %%ecx,%%ecx \n\t"   \
                                "cpuid              \n\t"   \
                                :"=d"(APICID)::"%rax","%rbx","%rcx"); \
                    }while(0)


extern cpu_info_t cpu_info;


#endif