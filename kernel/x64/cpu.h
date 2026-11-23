#pragma once
#include "../mm/alloc_percpu.h"
#include "config.h"
#include "gdt_tss.h"
#include "irq_dispatcher.h"
#include "moslib.h"
#include "msr.h"
#include "pmm.h"
#include "rbtree.h"
#include "vmm.h"
#include "../time/time_core.h"

/* =========================================================================
 * CPU 拓扑与掩码常量定义
 * ========================================================================= */
#define CPU_INVALID_ID              0xFFFFFFFFU                     /* 无效逻辑核心号 / 无效 APIC ID 哨兵 */

/* 按给定核心数向上取整计算位图所需的 uint64 字数 */
#define CPUMASK_WORDS_FOR(nr_cpus)  (((uint32)(nr_cpus) + 63U) / 64U)

/* cpu_hw_t.flags 内部状态位 */
#define CPU_HW_FLAG_PRESENT         (1U << 0)       /* 开机时硬件已启用 (对应 MADT Bit 0: Enabled) */
#define CPU_HW_FLAG_ONLINE_CAPABLE  (1U << 1)       /* 支持运行期热插拔 (对应 MADT Bit 1: Online Capable) */
#define CPU_HW_FLAG_X2APIC          (1U << 2)       /* 来源包含 MADT Type 9 (32 位 x2APIC ID) */

/* =========================================================================
 * CPU 硬件拓扑描述符与全局拓扑结构
 * ========================================================================= */
typedef struct {
    uint32 apic_id;         /* 硬件 APIC / x2APIC ID */
    uint32 acpi_proc_id;    /* ACPI 处理器唯一 UID (用于 Type 0 / Type 9 去重与 NMI 绑定) */
    uint32 logical_id;      /* 内核连续逻辑编号 (0 恒为 BSP，范围 0 .. nr_possible_cpus - 1) */
    uint8  lint_nmi;        /* NMI 绑定的本地中断引脚 (0 = LINT0, 1 = LINT1, 0xFF = 未配置) */
    uint8  flags;           /* CPU_HW_FLAG_* 组合状态位 */
    uint16 _pad;            /* 对齐填充，保持结构体紧凑为 16 字节 */
} cpu_hw_t;

typedef struct {
    cpu_hw_t  *cpu_hw;                  /* 动态硬件拓扑数组，长度严格由 nr_possible_cpus 决定 */
    uint32    *cpu_logicid_to_apicid;   /* 动态映射表：逻辑核号 -> 硬件 APIC ID */
    uint64    *cpu_possible_mask;       /* 动态位图：所有可能存在的 CPU (含热插拔预留核) */
    uint64    *cpu_present_mask;        /* 动态位图：开机时已启用的物理/虚拟 CPU */
    uint64    *cpu_online_mask;         /* 动态位图：当前已上线运行调度的 CPU */
    uint32     nr_possible_cpus;        /* 系统最大可能核心数 (Enabled + Online Capable，封顶 CONFIG_NR_CPUS) */
    uint32     nr_present_cpus;         /* 开机时已启用的物理/虚拟核心数 (仅统计 Enabled == 1) */
    uint32     nr_online_cpus;          /* 当前已完成初始化并在线运行的核心数 */
} cpu_topo_t;

/* 全局拓扑实例与虚拟内存布局 */
extern cpu_topo_t  cpu_topo;
extern vm_layout_t vm_layout;

/* =========================================================================
 * 动态 CPU 位图操作内联辅助函数 (受 NULL 与 nr_possible_cpus 双重越界保护)
 * ========================================================================= */

/* 获取当前拓扑下每张动态位图包含的 uint64 字数 */
static inline uint32 cpumask_nr_words(void) {
    return CPUMASK_WORDS_FOR(cpu_topo.nr_possible_cpus);
}

static inline void cpumask_set_cpu(uint64 *mask, uint32 cpu) {
    if (mask != NULL && cpu < cpu_topo.nr_possible_cpus) {
        mask[cpu >> 6] |= (1ULL << (cpu & 63U));
    }
}

static inline void cpumask_clear_cpu(uint64 *mask, uint32 cpu) {
    if (mask != NULL && cpu < cpu_topo.nr_possible_cpus) {
        mask[cpu >> 6] &= ~(1ULL << (cpu & 63U));
    }
}

static inline boolean cpumask_test_cpu(const uint64 *mask, uint32 cpu) {
    if (mask == NULL || cpu >= cpu_topo.nr_possible_cpus) {
        return FALSE;
    }
    return ((mask[cpu >> 6] & (1ULL << (cpu & 63U))) != 0) ? TRUE : FALSE;
}

/* 原子标记指定逻辑核心上线 (供多核 AP 并发启动或热插拔上线时调用) */
static inline void cpu_mark_online(uint32 logical_id) {
    if (cpu_topo.cpu_online_mask == NULL || logical_id >= cpu_topo.nr_possible_cpus) {
        return;
    }
    uint64 bit = (1ULL << (logical_id & 63U));
    uint64 old = __atomic_fetch_or(&cpu_topo.cpu_online_mask[logical_id >> 6], bit, __ATOMIC_SEQ_CST);
    if ((old & bit) == 0) {
        __atomic_add_fetch(&cpu_topo.nr_online_cpus, 1, __ATOMIC_SEQ_CST);
    }
}

/* =========================================================================
 * 逻辑 ID <-> 硬件 APIC ID 互查接口 (全部基于动态表 cpu_topo.cpu_logicid_to_apicid)
 * ========================================================================= */

/* 逻辑核心号 -> 硬件 APIC ID (未初始化或越界返回 CPU_INVALID_ID) */
static inline uint32 cpu_lid_to_aid(uint32 lid) {
    if (cpu_topo.cpu_logicid_to_apicid == NULL || lid >= cpu_topo.nr_possible_cpus) {
        return CPU_INVALID_ID;
    }
    return cpu_topo.cpu_logicid_to_apicid[lid];
}

/* 硬件 APIC ID -> 逻辑核心号 (未命中返回 CPU_INVALID_ID) */
static inline uint32 cpu_aid_to_lid(uint32 aid) {
    if (cpu_topo.cpu_logicid_to_apicid == NULL) {
        return CPU_INVALID_ID;
    }
    for (uint32 i = 0; i < cpu_topo.nr_possible_cpus; i++) {
        if (cpu_topo.cpu_logicid_to_apicid[i] == aid) {
            return i;
        }
    }
    return CPU_INVALID_ID;
}

/* 解析 ACPI MADT，统计三项核数并通过 memblock 动态分配全部拓扑与掩码数组 */
void cpu_topo_init(void);

/* =========================================================================
 * Per-CPU 调度队列、定时队列与核心控制块 (CCB)
 * ========================================================================= */

/* 每核 EEVDF 就绪队列 */
typedef struct run_queue_t {
    rb_root_t sched_tree;       /* EEVDF 就绪任务红黑树 (以虚拟截止时间 Ve 为 Key) */
    uint64    vtime;            /* 当前运行队列的虚拟时钟基准 (V) */
} run_queue_t;

/* 每核高精度睡眠定时队列 */
typedef struct sleep_queue_t {
    rb_root_t  sleep_tree;      /* 睡眠任务红黑树 (以 wake_up_ns 为 Key) */
    rb_node_t *rb_leftmost;     /* 缓存的最左节点指针 (O(1) 获取最近到期任务) */
    uint64     next_wake_ns;    /* 下一次最近唤醒绝对时间戳 (纳秒) */
} sleep_queue_t;

/* 核心运行生命周期状态 */
typedef enum {
    CPU_STATE_OFFLINE = 0,      /* 尚未唤醒或已热拔下线 */
    CPU_STATE_BOOTING,          /* AP 启动引导中 */
    CPU_STATE_IDLE,             /* 空闲态 (执行 hlt) */
    CPU_STATE_RUNNING,          /* 正在执行内核态或用户态任务 */
    CPU_STATE_PANIC             /* 核心已崩溃停机 */
} cpu_state_t;

/* 每 CPU 控制块 (单结构体·冷热字段分离布局，位于各核 Per-CPU 槽位偏移 0 处) */
typedef struct cpu_control_block_t {
    /* --- 热数据区：中断/系统调用/上下文切换极高频访问 --- */
    struct cpu_control_block_t *self;       /* 自引用线性地址指针 (快速从 %gs 还原常规指针) */
    uint64          hwirq_rsp_top;          /* 硬件中断独立栈顶 */
    uint64          kernel_rsp_top;         /* 当前线程内核栈顶 (与 tss.rsp0 保持同步) */
    uint64          tmp_user_rsp;           /* SYSCALL 入口暂存用户态 RSP 的草稿槽 */
    uint32          logical_id;             /* 内核逻辑编号 (0 永远为 BSP) */
    uint32          apic_id;                /* 硬件 APIC / x2APIC ID (发送 IPI 高频使用) */
    uint8           numa_node;              /* 所属逻辑 NUMA 节点号 */
    cpu_state_t     state;                  /* 当前核心运行状态 */
    clockevent_t   *active_ce;              /* 本核当前激活的时钟事件设备 */
    uint64          next_deadline_ns;       /* 下一次定时中断到期时间 (纳秒) */

    /* --- 硬件描述符区 (按 64 字节缓存行独立对齐) --- */
    gdt_t           gdt __attribute__((aligned(64))); /* 本核独立 GDT 表 */
    tss_t           tss __attribute__((aligned(64))); /* 本核独立 TSS (含 RSP0 / IST 栈) */

    /* --- 调度与高频统计区 --- */
    void           *kmem_cache_cpu;         /* SLUB 每核无锁对象池指针 */
    uint64          timer_ticks;            /* Local APIC Timer 累计滴答数 */
    uint64          interrupt_count;        /* 本核累计响应中断次数 */
    uint64          context_switches;       /* 本核累计上下文切换次数 */
    struct task_t  *cur_task;               /* 当前正在本核运行的任务 */
    struct task_t  *idle_task;              /* 本核兜底空闲任务 (idle) */
    run_queue_t     run_queue;              /* EEVDF 就绪队列 */
    sleep_queue_t   sleep_queue;            /* 睡眠定时队列 */

    /* --- 冷数据区：中断路由表、固件档案与页帧缓存 --- */
    uint32          acpi_proc_id;           /* ACPI 处理器 UID */
    irq_desc_t      irq_table[223];         /* 外部中断向量路由表 (向量 32..254，共 223 项) */
    uint32          irq_lock;               /* 中断路由表自旋锁 */
    uint8           lint_nmi;               /* NMI 绑定的 LINT 引脚号 */
    char8           manufacturer_name[13];  /* 厂商字符串 (如 "AuthenticAMD\0" / "GenuineIntel\0") */
    char8           model_name[49];         /* 处理器品牌型号字符串 */
    uint32          fundamental_mhz;        /* 标称基础主频 (MHz) */
    uint32          maximum_mhz;            /* 最大睿频 (MHz) */
    uint32          bus_mhz;                /* 总线基准频率 (MHz) */
    pcp_pageset_t   pcp;                    /* Buddy Per-CPU 页帧本地缓存 */
} __attribute__((aligned(64))) cpu_control_block_t;

/* =========================================================================
 * %gs Per-CPU 寻址与 GS_BASE 寄存器内联原语
 * ========================================================================= */

/* 通过 %gs 段偏移 0 直接单指令访问本核控制块 */
#define THIS_CCB ((cpu_control_block_t __seg_gs *)0)

/* 读取本核 CCB 的常规高半区线性地址指针 */
static inline cpu_control_block_t *this_ccb(void) {
    return THIS_CCB->self;
}

/* 将动态 alloc_percpu 返回的逻辑偏移量还原为当前核的常规线性指针 */
static inline void *this_cpu_ptr(void *offset) {
    return (void *)((uint64)THIS_CCB->self + (uint64)offset);
}

/* 按逻辑核心号获取任意核心的 CCB 线性地址 (使用 64 位乘法防大核数溢出) */
static inline cpu_control_block_t *cpu_lid_by_ccb(uint32 lid) {
    return (cpu_control_block_t *)(vm_layout.percpu_vaddr_start + (uint64)lid * PERCPU_WINDOW_SIZE);
}

static inline uint64 asm_rdgsbase(void) {
    uint64 gsbase;
    __asm__ __volatile__(
        "rdgsbase %0 \n\t"
        : "=r"(gsbase)
        :
        : "cc"
    );
    return gsbase;
}

static inline void asm_wrgsbase(void *gsbase) {
    __asm__ __volatile__(
        "wrgsbase %0 \n\t"
        :
        : "r"(gsbase)
        : "cc"
    );
}

static inline void asm_swapgs(void) {
    __asm__ __volatile__(
        "swapgs \n\t"
        :
        :
        : "memory"
    );
}

/* 将指定逻辑核的 Per-CPU 槽位基址写入当前核的 GS_BASE，并清零 KERNEL_GS_BASE */
static inline void set_gs_base(uint32 logical_id) {
    void *per_cpu_base = (void *)(vm_layout.percpu_vaddr_start + (uint64)logical_id * PERCPU_WINDOW_SIZE);
    asm_wrgsbase(per_cpu_base);
    asm_wrmsr(KERNEL_GS_BASE_MSR, 0);
}