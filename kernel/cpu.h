#ifndef _CPU_H
#define _CPU_H
#include "moslib.h"

#define IA32_APIC_BASE  0x1B
#define IA32_EFER       0xC0000080
#define IA32_STAR       0xC0000081
#define IA32_LSTAR      0xC0000082
#define IA32_FMASK      0xC0000084

#define SET_CR0(VALUE) __asm__ __volatile__ ("movq   %0,%%cr0  \n\t" ::"r"(VALUE):"memory")
#define GET_CR0(VALUE) __asm__ __volatile__ ("movq   %%cr0,%0  \n\t" :"=r"(VALUE)::"memory")
#define SET_CR3(phy_addr) __asm__ __volatile__("mov %0,%%cr3"::"r"(phy_addr):"memory");
#define GET_CR3(phy_addr) __asm__ __volatile__("mov %%cr3,%0":"=r"(phy_addr)::"memory");
#define SET_CR4(VALUE) __asm__ __volatile__ ("movq   %0,%%cr4  \n\t" ::"r"(VALUE):"memory")
#define GET_CR4(VALUE) __asm__ __volatile__ ("movq   %%cr4,%0  \n\t" :"=r"(VALUE)::"memory")
#define XSETBV(EAX,EDX,ECX) __asm__ __volatile__("xsetbv \n\t" ::"a"(EAX),"d"(EDX),"c"(ECX):"memory")
#define XGETBV(EAX,EDX,ECX) __asm__ __volatile__("xgetbv \n\t" :"=a"(EAX),"=d"(EDX):"c"(ECX):"memory")
#define WRMSR(EAX,EDX,ECX) __asm__ __volatile__("wrmsr \n\t" ::"a"(EAX),"d"(EDX),"c"(ECX):"memory")
#define RDMSR(EAX,EDX,ECX) __asm__ __volatile__("rdmsr \n\t" :"=a"(EAX),"=d"(EDX):"c"(ECX):"memory")
#define CPUID(OUT_EAX,OUT_EBX,OUT_ECX,OUT_EDX,IN_EAX,IN_ECX) __asm__ __volatile__("cpuid \n\t" :"=a"(OUT_EAX),"=b"(OUT_EBX),"=c"(OUT_ECX),"=d"(OUT_EDX):"a"(IN_EAX),"c"(IN_ECX):"memory")

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