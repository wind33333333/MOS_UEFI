#pragma once
#include "moslib.h"

// =====================================================================
// x86_64 FS/GS Base Model-Specific Registers (MSRs)
// =====================================================================

/*
 * MSR_FS_BASE: FS 段的 64 位基地址
 * [用途]：通常指向当前用户态线程的 TLS (Thread-Local Storage)。
 * [注意]：用户态程序在开启 FSGSBASE 特性后，用 wrfsbase 指令修改的就是这个值；
 *        内核进行线程切换时，也需要读写这个寄存器。
 */
#define FS_BASE_MSR             0xC0000100

/*
 * MSR_GS_BASE: 当前【激活状态】的 GS 段 64 位基地址
 * [用途]：正在起作用的 GS 基址。
 *        - 在 Ring 3 (用户态) 时：它通常为 0，或者指向用户态特定结构。
 *        - 在 Ring 0 (内核态) 时：它指向当前 CPU 的 cpu_core_t。
 */
#define GS_BASE_MSR             0xC0000101

/*
 * MSR_KERNEL_GS_BASE: 隐藏的、备用的 GS 基地址保险箱
 * [用途]：专门为 swapgs 指令设计的“影子寄存器”。
 * [机制]：执行 swapgs 汇编指令时，CPU 硬件会自动且瞬间将 MSR_GS_BASE 的值
 *        与 MSR_KERNEL_GS_BASE 的值进行互换。
 */
#define KERNEL_GS_BASE_MSR      0xC0000102



#define PAT_MSR        0x277        //设置页属性PAT类型
#define EFER_MSR       0xC0000080   // 扩展功能寄存器（Extended Feature Enable Register）
#define STAR_MSR       0xC0000081   // 系统调用目标寄存器（Segment Target Address Register）
#define LSTAR_MSR      0xC0000082   // 64位系统调用入口寄存器（Long Mode System Call Target Address Register）
#define CSTAR_MSR      0xC0000083   // 兼容模式系统调用入口寄存器（Compatibility Mode System Call Target Address Register）
#define FMASK_MSR      0xC0000084   // 系统调用掩码寄存器（System Call Flag Mask Register）
#define APIC_BASE_MSR                    0x1B   // 本地APIC基地址寄存器（Local APIC Base Address Register）
#define TSC_DEADLINE_MSR                 0x6E0  //TSC-Deadline时间戳计数器截止寄存器

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


static inline uint64 asm_rdmsr(uint32 msr) {
 uint32 low, high;
 __asm__ __volatile__(
     "rdmsr"
     : "=a"(low), "=d"(high)    // 明确告诉编译器：这行指令覆盖了 EAX(=a) 和 EDX(=d)！
     : "c"(msr)     // 输入 MSR 地址到 ECX(=c)
     : "memory"                 // 读外设可能产生副作用，加 memory 屏障
 );

 // 让 C 编译器去负责 64 位拼装，绝对不会出现寄存器污染！
 return ((uint64)high << 32) | low;
}

static inline void asm_wrmsr(uint32 msr, uint64 value) {
 uint32 low = (uint32)value;           // 自动截取低 32 位
 uint32 high = (uint32)(value >> 32);  // 获取高 32 位

 __asm__ __volatile__(
     "wrmsr"
     : // 没有输出
     : "a"(low), "d"(high), "c"(msr) // 明确绑定三个寄存器
     : "memory"
 );
}