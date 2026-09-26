#pragma once
#include "moslib.h"
#include "gdt_tss.h"


#define APIC_ONESHOT 0              //一次性定时模式
#define APIC_PERIODIC  0x20000      //周期性定时模式
#define APIC_TSC_DEADLINE 0x40000   //TSC截止期限模式


//中断结束发送EOI
static inline void apic_send_eoi(void) {
    __asm__ __volatile__(
        "xorl %%edx, %%edx \n\t"
        "xorl %%eax, %%eax \n\t"
        "movl $0x80B, %%ecx \n\t"
        "wrmsr             \n\t"
        : /* 无输出 */
        : /* 无输入 */
        : "%rax", "%rcx", "%rdx", "memory" // 明确告知编译器这三个寄存器被破坏了
    );
}


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


static inline uint64 asm_rdgsbase(void) {
    uint64 gsbase;
    __asm__ __volatile__(
        "rdgsbase %0  \n\t"
        :"=r"(gsbase)
        :
        :"cc");
    return gsbase;
}

static inline void asm_wrgsbase(void* gsbase) {
    __asm__ __volatile__(
        "wrgsbase %0  \n\t"
        :
        :"r"(gsbase)
        :"cc");
}

static inline void asm_swapgs() {
    __asm__ __volatile__(
        "swapgs  \n\t"
        :
        :
        :"memory");
}


// 定义 CPU 的生命周期状态
typedef enum {
    CPU_STATE_OFFLINE = 0, // 尚未唤醒或已下线
    CPU_STATE_BOOTING,     // 正在执行启动代码 (AP 唤醒中)
    CPU_STATE_IDLE,        // 闲置中 (执行 hlt 指令)
    CPU_STATE_RUNNING,     // 正在执行用户态或内核态任务
    CPU_STATE_PANIC        // 该核心已崩溃停止
} cpu_state_t;

// =================================================================
// 👑 核心数据结构：每 CPU 控制块 (单结构体·内部冷热隔离版)
// =================================================================
typedef struct cpu_core {
    // =============================================================
    // 🔥 第一层：极热调度区 (Hot Zone - 严格控制在第一个 64B 缓存行内)
    // =============================================================
    uint32          logical_id;           // [0x00] 内核逻辑编号 (0 永远是 BSP)
    uint32          apic_id;              // [0x04] 物理 APIC ID (发 IPI 高频使用)
    cpu_state_t     state;                // [0x08] 核心运行状态 (假设 enum 为 4 字节)
    uint32          numa_node;            // [0x0C] 所属 NUMA 节点 (内存分配高频使用)

    // 💡 预留给 syscall 汇编入口的极速切栈跳板
    uint64          current_kernel_stack; // [0x10] 当前线程内核栈顶 (与 tss->rsp0 同步)
    uint64          user_rsp_scratch;     // [0x18] syscall 发生时暂存用户态 RSP 的草稿箱

    struct thread_t *current_thread;      // [0x20] 当前正在运行的线程
    struct thread_t *idle_thread;         // [0x28] 专属空闲线程
    tss_t           *tss;                 // [0x30] 专属 TSS 指针 (中断切栈高频修改)
    void            *kmem_cache_cpu;      // [0x38] SLUB 无锁内存池指针
    // -------- 👆 以上刚好 64 字节 (0x00 ~ 0x3F)，完美填满第 1 个 Cache Line！ --------

    // =============================================================
    // 🌡️ 第二层：温数据/高频统计区 (Warm Zone - 第 2 个 64B 缓存行)
    // =============================================================
    uint64          timer_ticks;          // Local APIC Timer 滴答数
    uint64          interrupt_count;      // 处理的总中断次数
    uint64          context_switches;     // 上下文切换次数
    // void         *run_queue;           // 未来可放这里：就绪队列指针

    // =============================================================
    // 🧊 第三层：冰封档案区 (Cold Zone - 强制推到新的 64B 缓存行边界)
    // 开机初始化或查系统信息时才用，平时绝不占用 L1 Cache！
    // =============================================================
    gdt_t           *gdt_base __attribute__((aligned(64))); // 强制对齐隔离！
    uint32          acpi_proc_id;         // 从顶部挪下来的冷数据：ACPI 处理器 ID
    uint8           lint_nmi;             // 从顶部挪下来的冷数据：NMI 引脚号

    char8           manufacturer_name[13];// 例如 "GenuineIntel\0" (13B)
    char8           model_name[49];       // 处理器具体型号字符串 (49B)

    // ✅ 修复溢出：TSC 用 64 位存精确 Hz，其余用 32 位存 MHz（完美契合 CPUID 0x16）
    uint64          tsc_hz;               // 精确到 Hz，支持超过 4.29GHz 的高频核心
    uint32          fundamental_mhz;      // 基础频率 (MHz)
    uint32          maximum_mhz;          // 最大睿频 (MHz)
    uint32          bus_mhz;              // 总线/外频 (MHz)

} __attribute__((aligned(64))) cpu_core_t;

// =======================================================
// 💡 架构师魔法宏：定义一个永远指向当前 CPU 的“魔术指针”
// =======================================================
// 这里把 0 强制转换为一个基于 GS 的指针。因为 GS_BASE 已经指向了结构体首地址，所以偏移量为 0！
#define THIS_CPU ((cpu_core_t __seg_gs *)0)

extern uint32 active_cpu_count;
extern cpu_core_t *cpu_cores;



