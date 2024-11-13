#ifndef __ACPI_H__
#define __ACPI_H__
#include "moslib.h"

void init_apic(void);
void enable_apic_time (UINT64 time,UINT32 model,UINT32 ivt);

#define APIC_ID_MSR                       0x802  // 本地APIC ID寄存器
#define APIC_VERSION_MSR                  0x803  // 本地APIC版本寄存器
#define APIC_TASK_PRIORITY_MSR            0x808  // 任务优先级寄存器
#define APIC_PROCESSOR_PRIORITY_MSR       0x80A  // 处理器优先级寄存器
#define APIC_EOI_MSR                      0x80B  // 中断结束（EOI）寄存器
#define APIC_SPURIOUS_VECTOR_MSR          0x80F  // 虚假中断向量寄存器

// 中断服务寄存器（ISR）
#define APIC_ISR_31_0_MSR                 0x810  // ISR位31:0
#define APIC_ISR_63_32_MSR                0x811  // ISR位63:32
#define APIC_ISR_95_64_MSR                0x812  // ISR位95:64
#define APIC_ISR_127_96_MSR               0x813  // ISR位127:96
#define APIC_ISR_159_128_MSR              0x814  // ISR位159:128
#define APIC_ISR_191_160_MSR              0x815  // ISR位191:160
#define APIC_ISR_223_192_MSR              0x816  // ISR位223:192
#define APIC_ISR_255_224_MSR              0x817  // ISR位255:224

// 中断请求寄存器（IRR）
#define APIC_IRR_31_0_MSR                 0x820  // IRR位31:0
#define APIC_IRR_63_32_MSR                0x821  // IRR位63:32
#define APIC_IRR_95_64_MSR                0x822  // IRR位95:64
#define APIC_IRR_127_96_MSR               0x823  // IRR位127:96
#define APIC_IRR_159_128_MSR              0x824  // IRR位159:128
#define APIC_IRR_191_160_MSR              0x825  // IRR位191:160
#define APIC_IRR_223_192_MSR              0x826  // IRR位223:192
#define APIC_IRR_255_224_MSR              0x827  // IRR位255:224

// 中断屏蔽寄存器（TMR）
#define APIC_TMR_31_0_MSR                 0x818  // TMR位31:0
#define APIC_TMR_63_32_MSR                0x819  // TMR位63:32
#define APIC_TMR_95_64_MSR                0x81A  // TMR位95:64
#define APIC_TMR_127_96_MSR               0x81B  // TMR位127:96
#define APIC_TMR_159_128_MSR              0x81C  // TMR位159:128
#define APIC_TMR_191_160_MSR              0x81D  // TMR位191:160
#define APIC_TMR_223_192_MSR              0x81E  // TMR位223:192
#define APIC_TMR_255_224_MSR              0x81F  // TMR位255:224

#define APIC_ERROR_STATUS_MSR             0x828  // 错误状态寄存器
#define APIC_LVT_CMCI_MSR                 0x82F  // LVT校验错误寄存器

#define APIC_INTERRUPT_COMMAND_MSR        0x830  // 中断命令寄存器（ICR）

// 本地向量表（LVT）寄存器
#define APIC_LVT_TIMER_MSR                0x832  // 定时器LVT寄存器
#define APIC_LVT_THERMAL_SENSOR_MSR       0x833  // 热传感器LVT寄存器
#define APIC_LVT_PERF_COUNTER_MSR         0x834  // 性能计数器LVT寄存器
#define APIC_LVT_LINT0_MSR                0x835  // 本地中断LINT0寄存器
#define APIC_LVT_LINT1_MSR                0x836  // 本地中断LINT1寄存器
#define APIC_LVT_ERROR_MSR                0x837  // 错误LVT寄存器

#define APIC_INITIAL_COUNT_MSR            0x838  // 初始计数寄存器
#define APIC_CURRENT_COUNT_MSR            0x839  // 当前计数寄存器
#define APIC_DIVIDE_CONFIG_MSR            0x83E  // 分频配置寄存器
#define APIC_SELF_IPI_MSR                 0x83F  // 自发送IPI寄存器

//中断结束发送EOI
#define EOI() \
        do {  \
          __asm__ __volatile__( \
           "xorl     %%edx,%%edx   \n\t" \
           "xorl     %%eax,%%eax   \n\t" \
           "movl     $0x80B,%%ecx  \n\t" \
           "wrmsr                  \n\t"  \
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
        "rdtscp                           \n\t" \
        "shll       $32,%%rdx             \n\t" \
        "orq        %%rdx,%%rax           \n\t" \
        "addq       %0,%%rax              \n\t" \
        "movq       %%rax,%%rdx           \n\t" \
        "movq       $0xFFFFFFFF,%%rcx     \n\t" \
        "andq       %%rcx,%%rax           \n\t" \
        "shrq       $32,%%rdx             \n\t" \
        "movq       $0x6E0,%%ecx          \n\t" /*IA32_TSC_DEADLINE寄存器 TSC-Deadline定时模式 */ \
        "wrmsr                            \n\t" \
        ::"m"(TIME):"%rax","%rcx","%rdx"); \
        } while(0)

#define APIC_ONESHOT 0              //一次性定时模式
#define APIC_PERIODIC  0x20000      //周期性定时模式
#define APIC_TSC_DEADLINE 0x40000   //TSC截止期限模式

#endif
