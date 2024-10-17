#ifndef __ACPI_H__
#define __ACPI_H__

#include "moslib.h"
#include "hpet.h"
#include "interrupt.h"
#include "cpu.h"

void init_apic(void);
void enable_apic_time (UINT64 time,UINT32 model,UINT32 ivt);

//中断结束发送EOI
#define EOI() \
        do {  \
          __asm__ __volatile__( \
           "xor     %%edx,%%edx \n\t" \
           "xor     %%eax,%%eax \n\t" \
           "mov     $0x80B,%%ecx \n\t" \
           "wrmsr      \n\t"  \
            :::"%rdx","%rax");    \
          } while(0)


#define DISABLE_APIC_TIME() \
        do {  \
          __asm__ __volatile__( \
         "xorl   %%edx,%%edx           \n\t"          \
         "movl   $0x10000,%%eax        \n\t"         /*bit0-7中断向量号,bit16屏蔽标志 0未屏蔽 1屏蔽,bit17 18 00/一次计数 01/周期计数 10/TSC-Deadline*/ \
         "movl   $0x832,%%ecx          \n\t"         /*定时器模式配置寄存器*/ \
         "wrmsr                        \n\t"          \
          :::"%rdx","%rax");    \
          } while(0)

#define APIC_SET_TSCDEADLINE(TIME) \
        do {                       \
        __asm__ __volatile__( \
        "rdtscp                    \n\t" \
        "shl $32,%%rdx             \n\t" \
        "or %%rdx,%%rax            \n\t" \
        "add %0,%%rax              \n\t" \
        "mov %%rax,%%rdx           \n\t" \
        "mov $0xFFFFFFFF,%%rcx     \n\t" \
        "and %%rcx,%%rax           \n\t" \
        "shr $32,%%rdx             \n\t" \
        "mov $0x6E0,%%ecx          \n\t" /*IA32_TSC_DEADLINE寄存器 TSC-Deadline定时模式 */ \
        "wrmsr                     \n\t" \
        ::"m"(TIME):"%rax","%rcx","%rdx"); \
        } while(0)

#define APIC_ONESHOT 0              //一次性定时模式
#define APIC_PERIODIC  0x20000      //周期性定时模式
#define APIC_TSC_DEADLINE 0x40000   //TSC截止期限模式

#endif
