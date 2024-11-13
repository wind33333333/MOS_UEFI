#ifndef _CPU_H
#define _CPU_H
#include "moslib.h"

#define IA32_EFER   0xC0000080
#define IA32_STAR   0xC0000081
#define IA32_LSTAR  0xC0000082
#define IA32_FMASK  0xC0000084

#define WRMSR(EAX,EDX,ECX) \
                do{        \
                    __asm__ __volatile__( \
                        "wrmsr         \n\t"                    \
                        ::"a"(EAX),"d"(EDX),"c"(ECX):"memory"); \
                }while(0)

#define RDMSR(EAX,EDX,ECX)  \
                do{         \
                    __asm__ __volatile__( \
                        "rdmsr         \n\t"                    \
                        :"=a"(EAX),"=d"(EDX):"c"(ECX):"memory"); \
                }while(0)

#define GET_APICID(APICID) \
                    do{    \
                        __asm__ __volatile__(               \
                                "movl   $0xb,%%eax   \n\t"   \
                                "movl   $1,%%ecx     \n\t"   \
                                "cpuid               \n\t"   \
                                :"=d"(APICID)::"%rax","%rbx","%rcx","memory"); \
                    }while(0)


void init_cpu(void);
void init_cpu_amode(void);
void get_cpu_info(void);
void print_h(void);
void user_program(void);
extern  void syscall_entry(void);


typedef struct {
    CHAR8 manufacturer_name[13];
    CHAR8 model_name[49];
    UINT32 fundamental_frequency;
    UINT32 maximum_frequency;
    UINT32 logical_processors_number;
    UINT32 bus_frequency;
    UINT32 tsc_frequency;
}cpu_info_t;

extern cpu_info_t cpu_info;

#endif