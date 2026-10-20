#pragma once
#include "moslib.h"
#include "gdt_tss.h"
#include "rbtree.h"
#include "../time/time_core.h"


#define APIC_ONESHOT        (0 << 17)              //一次性定时模式
#define APIC_PERIODIC       (1 << 17)              //周期性定时模式
#define APIC_TSC_DEADLINE   (2 << 17)              //TSC截止期限模式

#define APIC_DIV_BY_1             0x0B


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


// 调度队列 (Per-CPU)
typedef struct run_queue_t{
    rb_root_t sched_tree;       // EEVDF就绪任务红黑树 (以 Ve 为 Key)
    uint64 vtime;               // 系统当前的全局虚拟时间 (V)
}run_queue_t;


//定时队列
typedef struct sleep_queue_t{
    rb_root_t       sleep_tree;       // 睡眠红黑树 (以 wake_up_ns 为 Key)
    rb_node_t       *rb_leftmost;     // 缓存的最左红黑树节点指针
    uint64          next_wake_ns;
}sleep_queue_t;


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
typedef struct cpu_core_t {
    uint64          kernel_rsp_top;       //当前线程内核栈顶 (与 tss->rsp0 同步)
    uint64          cur_user_rsp;         //syscall 发生时暂存用户态 RSP 的草稿箱
    struct cpu_core_t *self;              //指向自己
    uint32          logical_id;           //内核逻辑编号 (0 永远是 BSP)
    uint32          apic_id;              // 物理 APIC ID (发 IPI 高频使用)
    cpu_state_t     state;                //核心运行状态 (假设 enum 为 4 字节)
    uint32          numa_node;            // 所属 NUMA 节点 (内存分配高频使用)
    clockevent_t    *active_ce;
    uint64          next_deadline_ns;
    tss_t           *tss;                 // 专属 TSS 指针 (中断切栈高频修改)
    void            *kmem_cache_cpu;      // SLUB 无锁内存池指针
    uint64          timer_ticks;          // Local APIC Timer 滴答数
    uint64          interrupt_count;      // 处理的总中断次数
    uint64          context_switches;     // 上下文切换次数
    struct task_t   *cur_task;            // 当前正在 CPU 上飞驰的任务
    struct task_t   *idle_task;          // 兜底的系统空闲任务 (hlt)
    run_queue_t     run_queue;           // 就绪队列
    sleep_queue_t   sleep_queue;         // 定时队列


    gdt_t           *gdt_base;            // 强制对齐隔离！
    uint32          acpi_proc_id;         // 从顶部挪下来的冷数据：ACPI 处理器 ID
    uint8           lint_nmi;             // 从顶部挪下来的冷数据：NMI 引脚号

    char8           manufacturer_name[13];// 例如 "GenuineIntel\0" (13B)
    char8           model_name[49];       // 处理器具体型号字符串 (49B)

    uint32          fundamental_mhz;      // 基础频率 (MHz)
    uint32          maximum_mhz;          // 最大睿频 (MHz)
    uint32          bus_mhz;              // 总线/外频 (MHz)

}cpu_core_t;

extern cpu_core_t *cpu_cores;
extern uint32 active_cpu_count;

#define THIS_CPU ((cpu_core_t __seg_gs *)0) // 这里把 0 强制转换为一个基于 GS 的指针。因为 GS_BASE 已经指向了结构体首地址，所以偏移量为 0！

static inline cpu_core_t* this_cpu_ptr(void) {
    return THIS_CPU->self;
}



