#ifndef _CPU_H
#define _CPU_H
#include "moslib.h"

#define IA32_MTRR_MSR       0x277        //设置页属性PAT类型
#define IA32_EFER_MSR       0xC0000080   // 扩展功能寄存器（Extended Feature Enable Register）
#define IA32_STAR_MSR       0xC0000081   // 系统调用目标寄存器（Segment Target Address Register）
#define IA32_LSTAR_MSR      0xC0000082   // 64位系统调用入口寄存器（Long Mode System Call Target Address Register）
#define IA32_CSTAR_MSR      0xC0000083   // 兼容模式系统调用入口寄存器（Compatibility Mode System Call Target Address Register）
#define IA32_FMASK_MSR      0xC0000084   // 系统调用掩码寄存器（System Call Flag Mask Register）

#define SET_CR0(VALUE) __asm__ __volatile__ ("movq   %0,%%cr0  \n\t" ::"r"((UINT64)VALUE):"memory")
#define GET_CR0(VALUE) __asm__ __volatile__ ("movq   %%cr0,%0  \n\t" :"=r"((UINT64)VALUE)::"memory")
#define SET_CR3(PHY_ADDRESS) __asm__ __volatile__("mov %0,%%cr3"::"r"((UINT64)PHY_ADDRESS):"memory");
#define GET_CR3(PHY_ADDRESS) __asm__ __volatile__("mov %%cr3,%0":"=r"((UINT64)PHY_ADDRESS)::"memory");
#define SET_CR4(VALUE) __asm__ __volatile__ ("movq   %0,%%cr4  \n\t" ::"r"((UINT64)VALUE):"memory")
#define GET_CR4(VALUE) __asm__ __volatile__ ("movq   %%cr4,%0  \n\t" :"=r"((UINT64)VALUE)::"memory")
#define XSETBV(ADDRESS,VALUE) __asm__ __volatile__("xsetbv \n\t" ::"a"(((UINT64)VALUE)&0xFFFFFFFFUL),"d"(((UINT64)VALUE)>>32),"c"((UINT32)ADDRESS):"memory")
#define XGETBV(ADDRESS,VALUE) __asm__ __volatile__("xgetbv \n\t" "shlq $32,%%rdx \n\t" "orq %%rdx,%%rax \n\t" :"=a"(((UINT64)VALUE)):"c"((UINT32)ADDRESS):"memory")
#define WRMSR(ADDRESS,VALUE) __asm__ __volatile__("wrmsr \n\t" ::"a"(((UINT64)VALUE)&0xFFFFFFFFUL),"d"(((UINT64)VALUE)>>32),"c"((UINT32)ADDRESS):"memory")
#define RDMSR(ADDRESS,VALUE) __asm__ __volatile__("rdmsr \n\t" "shlq $32,%%rdx \n\t" "orq %%rdx,%%rax \n\t" :"=a"(((UINT64)VALUE)):"c"((UINT32)ADDRESS):"memory")
#define CPUID(IN_EAX,IN_ECX,OUT_EAX,OUT_EBX,OUT_ECX,OUT_EDX) __asm__ __volatile__("cpuid \n\t" :"=a"((UINT32)OUT_EAX),"=b"((UINT32)OUT_EBX),"=c"((UINT32)OUT_ECX),"=d"((UINT32)OUT_EDX):"a"((UINT32)IN_EAX),"c"((UINT32)IN_ECX):"memory")



void init_cpu(void);
void init_cpu_amode(void);
void get_cpu_info(void);

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