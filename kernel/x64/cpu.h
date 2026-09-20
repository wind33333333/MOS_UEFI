#pragma once
#include "moslib.h"


// 前置声明任务/线程控制块 (你的调度器定义)
struct thread_t;
// 前置声明 x86_64 的任务状态段 (用于存放 rsp0 和 IST)
struct tss64_t;

// 定义 CPU 的生命周期状态
typedef enum {
    CPU_STATE_OFFLINE = 0, // 尚未唤醒或已下线
    CPU_STATE_BOOTING,     // 正在执行启动代码 (AP 唤醒中)
    CPU_STATE_IDLE,        // 闲置中 (执行 hlt 指令)
    CPU_STATE_RUNNING,     // 正在执行用户态或内核态任务
    CPU_STATE_PANIC        // 该核心已崩溃停止
} cpu_state_t;

// =================================================================
// 👑 核心数据结构：每 CPU 控制块 (Per-CPU Control Block)
// =================================================================
typedef struct cpu_core {
    // -------------------------------------------------------------
    // [1] 身份与拓扑区 (Hardware Identity & Topology)
    // -------------------------------------------------------------
    uint32  logical_id;       // 内核分配的逻辑编号 (0, 1, 2... 用于数组索引)
    uint32  apic_id;          // 真实的物理 APIC ID (用于发送 IPI 中断唤醒/TLB 刷新)
    uint32  acpi_proc_id;     // ACPI 逻辑 ID (从 MADT 读出)
    uint32  numa_node;        // 所属 NUMA 物理节点 (由 SRAT 表解析得出)
    boolean is_bsp;           // 是否为主核心 (Bootstrap Processor)

    // -------------------------------------------------------------
    // [2] 运行时状态区 (Runtime Status)
    // -------------------------------------------------------------
    cpu_state_t state;        // 当前核心的运行状态

    // -------------------------------------------------------------
    // [3] 调度器上下文 (Scheduler Context)
    // -------------------------------------------------------------
    struct thread_t *current_thread; // 当前正在该 CPU 上运行的线程/进程
    struct thread_t *idle_thread;    // 该 CPU 专属的空闲线程 (没有任务时执行 hlt)
    // void *run_queue;              // 进阶：该 CPU 专属的就绪任务队列 (无锁调度核心)

    // -------------------------------------------------------------
    // [4] x86_64 底层硬件区 (Architecture Specific)
    // -------------------------------------------------------------
    // 💡 呼应我们之前的讨论：栈和 TSS！
    struct tss64_t *tss;      // 该 CPU 专属的 TSS (里面存放着这个核心的 rsp0, IST 1~4)
    uint64  gdt_base;         // 该 CPU 专属的 GDT 基地址 (多核系统每个核要有独立的 GDT)

    // -------------------------------------------------------------
    // [5] 本地内存池 (SLUB 关联)
    // -------------------------------------------------------------
    void    *kmem_cache_cpu;  // SLUB 分配器为该 CPU 准备的【无锁内存池】指针

    // -------------------------------------------------------------
    // [6] 统计与性能监控 (Stats & Profiling)
    // -------------------------------------------------------------
    uint64  interrupt_count;  // 该 CPU 处理的总中断次数
    uint64  context_switches; // 该 CPU 发生的上下文切换次数
    uint64  timer_ticks;      // 属于该 CPU 的 Local APIC Timer 滴答数

} __attribute__((aligned(64))) cpu_core_t;
// 🌟 架构师细节：__attribute__((aligned(64))) 极其重要！
// 保证这个结构体按 CPU Cache Line (64字节) 对齐，防止极其致命的“伪共享 (False Sharing)”性能暴跌！


typedef struct {
    char8 manufacturer_name[13];
    char8 model_name[49];
    uint32 logical_processors_number;
    uint32 fundamental_hz;
    uint32 maximum_hz;
    uint32 bus_hz;
    uint32 tsc_hz;
}cpu_info_t;

extern cpu_info_t cpu_info;
extern uint32 *apic_id_table;

uint32 apicid_to_cpuid(uint32 apic_id);
uint32 cpuid_to_apicid(uint32 cpu_id);

