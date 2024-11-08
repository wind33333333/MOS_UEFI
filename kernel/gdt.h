#ifndef __GDT_INIT_H__
#define __GDT_INIT_H__
#include "moslib.h"

void    init_gdt(void);

typedef struct{
    UINT16 limit;
    UINT64 *base;
} __attribute__((packed)) gdt_ptr_t;

extern gdt_ptr_t gdt_ptr;

//TSS起始选择子
#define TSS_DESCRIPTOR_START_INDEX 10

#define CODE64_0 (TYPE_CODE64 | DPL_0 | S | P | L)                   //ring0 64位代码段
#define DATA64_0 (TYPE_DATA64 | DPL_0 | S | P)                       //ring0 64位数据段
#define CODE64_3 (TYPE_CODE64 | DPL_3 | S | P | L)                   //ring3 64位代码段
#define DATA64_3 (TYPE_DATA64 | DPL_3 | S | P)                       //ring3 64位数据段
#define CODE32_0 (TYPE_CODE32 | DPL_0 | S | P | LIMIT_4G | DB | G)   //ring0 32位代码段
#define DATA32_0 (TYPE_DATA32 | DPL_0 | S | P | LIMIT_4G | DB | G)   //ring0 32位数据段

#define TYPE_CODE64 (0x8UL <<40)
#define TYPE_DATA64 (0x2UL <<40)
#define TYPE_CODE32 (0xAUL <<40)
#define TYPE_DATA32 (0x2UL <<40)
#define S           (1UL << 44)
#define DPL_0       (0UL << 45)
#define DPL_3       (3UL <<45)
#define P           (1UL << 47)
#define LIMIT_4G    (0xFUL << 48 | 0xFFFF)
#define L           (1UL << 53)
#define DB          (1UL << 54)
#define G           (1UL << 55)

#define LGDT(GDT_PTR,CODE64_SEL,DATA64_SEL) \
            do{                                             \
                __asm__ __volatile__(                       \
                        "lgdt       (%0)                \n\t"    \
                        "pushq      %1                  \n\t"    \
                        "leaq       1f(%%rip),%%rax     \n\t"    \
                        "pushq      %%rax               \n\t"    \
                        "lretq                          \n\t"    \
                        "1:                             \n\t"    \
                        "mov        %2,%%ss             \n\t"    \
                        "mov        %2,%%ds             \n\t"    \
                        "mov        %2,%%es             \n\t"    \
                        ::"r"(&GDT_PTR),"r"(CODE64_SEL),"r"(DATA64_SEL):"%rax");  \
            }while(0)

#endif