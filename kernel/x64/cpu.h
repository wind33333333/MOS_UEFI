#pragma once
#include "gdt_tss.h"
#include "irq_dispatcher.h"
#include "moslib.h"
#include "msr.h"
#include "rbtree.h"
#include "vmm.h"
#include "../time/time_core.h"

// 定义cpu核心最大物理槽位上限，编译时自定义，64的倍数，64/128/256/512/1024...
#define CONFIG_NR_CPUS 512

// 定义一个极其轻量级的硬件拓扑描述符
typedef struct {
    uint32 apic_id;
    uint32 acpi_proc_id;
    uint32 logical_id;
    uint8  lint_nmi;
} cpu_hw_t;

typedef struct {
    cpu_hw_t cpu_hw[CONFIG_NR_CPUS];
    uint32 cpu_count;
}cpu_topo_t;

extern cpu_topo_t cpu_topo;
extern uint32 cpu_logicid_to_apicid[];

//cpu逻辑id转apic id
uint32 cpu_lid_to_aid(uint32 lid) {
    return cpu_logicid_to_apicid[lid];
}

//cpu apic id 转 逻辑id
uint32 cpu_aid_to_lid(uint32 aid) {
    for (uint32 i=0; i<cpu_topo.cpu_count; i++) {
        if (cpu_logicid_to_apicid[i] == aid) return i;
    }
}

void cpu_topo_init(void);

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
typedef struct cpu_control_block_t {
    struct cpu_control_block_t *self;               //指向自己
    uint64          hwirq_rsp_top;        //硬件中断专业栈
    uint64          kernel_rsp_top;       //当前线程内核栈顶 (与 tss->rsp0 同步)
    uint64          tmp_user_rsp;         //syscall发生时暂存用户态RSP的草稿箱
    uint32          logical_id;           //内核逻辑编号 (0 永远是 BSP)
    uint32          apic_id;              // 物理 APIC ID (发 IPI 高频使用)
    uint8           numa_node;
    cpu_state_t     state;                //核心运行状态 (假设 enum 为 4 字节)
    clockevent_t    *active_ce;
    uint64          next_deadline_ns;
    gdt_t           gdt __attribute__((aligned(64)));    // gdt描述符表
    tss_t           tss __attribute__((aligned(64)));    // TSS
    void            *kmem_cache_cpu;      // SLUB 无锁内存池指针
    uint64          timer_ticks;          // Local APIC Timer 滴答数
    uint64          interrupt_count;      // 处理的总中断次数
    uint64          context_switches;     // 上下文切换次数
    struct task_t   *cur_task;            // 当前正在 CPU 上飞驰的任务
    struct task_t   *idle_task;          // 兜底的系统空闲任务 (hlt)
    run_queue_t     run_queue;           // 就绪队列
    sleep_queue_t   sleep_queue;         // 定时队列
    uint32          acpi_proc_id;         // 从顶部挪下来的冷数据：ACPI 处理器 ID
    irq_desc_t      irq_table[223];       //中断路由表 233 = 256-32异常（0-31）4-1个虚假中断（255号）
    uint32          irq_lock;
    uint8           lint_nmi;             // 从顶部挪下来的冷数据：NMI 引脚号
    char8           manufacturer_name[13];// 例如 "GenuineIntel\0" (13B)
    char8           model_name[49];       // 处理器具体型号字符串 (49B)
    uint32          fundamental_mhz;      // 基础频率 (MHz)
    uint32          maximum_mhz;          // 最大睿频 (MHz)
    uint32          bus_mhz;              // 总线/外频 (MHz)
}__attribute__((aligned(64)))cpu_control_block_t;

//通过gs直接访问cpu控制块
#define THIS_CCB ((cpu_control_block_t __seg_gs *)0)

//间接访问当前cpu控制块
static inline cpu_control_block_t* this_ccb(void) {
    return THIS_CCB->self;
}


static inline void* this_cpu_ptr(void* offfset) {
    return (void*)((uint64)THIS_CCB->self + (uint64)offfset);
}

extern vm_layout_t vm_layout;

static inline cpu_control_block_t* cpu_lid_by_ccb(uint32 lid) {
    return (cpu_control_block_t*)(vm_layout.percpu_vaddr_start + lid*(16*1024*1024));
};


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

static inline void set_gs_base(uint32 logical_id) {
    void* per_cpu_base = (void*)(vm_layout.percpu_vaddr_start+logical_id*(16*1024*1024));
    asm_wrgsbase(per_cpu_base);
    asm_wrmsr(KERNEL_GS_BASE_MSR,0);
}
