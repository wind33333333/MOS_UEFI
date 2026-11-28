#pragma once

#include "moslib.h"


/* =========================================================================
 * 1. 核心架构常量定义
 * ========================================================================= */

/* 单核固定 Per-CPU 虚拟窗口大小：128 MB (1 << 27)，天然满足 2 MB 大页对齐 */
#define PERCPU_WINDOW_SHIFT             27U
#define PERCPU_WINDOW_SIZE              (1ULL << PERCPU_WINDOW_SHIFT)

/* 最小分配粒度：8 字节 (1 bit = 8 B) */
#define PCPU_MIN_ALLOC_SHIFT            3U
#define PCPU_MIN_ALLOC_SIZE             (1U << PCPU_MIN_ALLOC_SHIFT)

/* 单次动态分配最大尺寸与最大对齐限制：32 KB */
#define PCPU_MAX_ALLOC_SIZE             (32U * 1024U)
#define PCPU_MAX_ALLOC_ALIGN            (32U * 1024U)

/* 启用前 2 MB 热点大页的物理内存门槛：平均每核至少 64 MB 物理内存（大页占比 <= 3.125%） */
#define PCPU_HUGE_RAM_PER_CPU_MIN       (64ULL * 1024ULL * 1024ULL)

/* x86-64 IA32_GS_BASE MSR 编号 */
#define MSR_IA32_GS_BASE                0xC0000101U

/* =========================================================================
 * 2. 静态声明与 %gs 单指令寻址宏（零 __typeof__，纯基础类型）
 * ========================================================================= */

/*
 * 静态 Per-CPU 变量声明宏：
 * 配合 Kernel.lds 中 .data..percpu 0x0 零基址段，
 * 静态变量取地址 &name 在链接期直接等价于相对 %gs 的偏移量。
 */
#define DEFINE_PER_CPU(type, name) \
    __attribute__((section(".data..percpu"), used)) type name

#define DEFINE_PER_CPU_ALIGNED(type, name, align) \
    __attribute__((section(".data..percpu"), aligned(align), used)) type name

#define DECLARE_PER_CPU(type, name) \
    extern type name

/* 全局 Per-CPU 虚拟窗口基址与核数（定义于 alloc_percpu.c，初始化后只读） */
extern uint64 g_pcpu_vaddr_base;
extern uint32 g_pcpu_nr_cpus;

/*
 * 本核单指令读写（GCC x86-64 __seg_gs 命名地址空间，直接生成 %gs:offset 指令）：
 *   type : 变量类型，例如 uint64 / uint32
 *   ptr  : Per-CPU 偏移指针（静态变量取地址 &var 或 alloc_percpu_align 返回值）
 */
#define this_cpu_read(type, ptr) \
    (*((const type __seg_gs *)(uint64)(ptr)))

#define this_cpu_write(type, ptr, val) \
    do { *((type __seg_gs *)(uint64)(ptr)) = (val); } while (0)

/*
 * 跨核寻址：直接通过编译期常量左移 27 位换算目标核的规范虚拟地址，无需查表。
 * 返回 void *，在 C 语言中可直接赋值给任意类型的目标指针。
 */
#define per_cpu_ptr(ptr, cpu) \
    ((void *)(g_pcpu_vaddr_base + \
              (((uint64)(cpu)) << PERCPU_WINDOW_SHIFT) + \
              (uint64)(ptr)))


/* =========================================================================
 * 3. 子系统对外生命周期与分配接口
 * ========================================================================= */

/*
 * 阶段一初始化（memblock 就绪后、Buddy 启动前调用）：
 *   vaddr_base      : Per-CPU 独立虚拟段起始地址（必须 2 MB 对齐）
 *   template_vaddr  : .data..percpu 静态初始模板的线性可读地址（__percpu_load）
 *   static_size     : 静态 .data..percpu 段总字节数（(uint64)__percpu_size）
 *   nr_cpus         : 系统最大可能逻辑核数
 *   total_ram_bytes : 系统可用物理内存总量（用于自动判定开启前 2 MB 大页或降级 4 KB）
 * 返回：TRUE 成功，FALSE 失败。
 */
boolean alloc_percpu_early_init(uint64 vaddr_base,
                                const void *template_vaddr,
                                uint64 static_size,
                                uint32 nr_cpus,
                                uint64 total_ram_bytes);

/* 为指定逻辑 CPU 核心写入 IA32_GS_BASE（BSP 与每个 AP 启动时各自调用一次） */
void alloc_percpu_setup_cpu_gs(uint32 cpu_id);

/* 阶段二激活（Buddy 初始化完成后调用，打开动态分配开关） */
void alloc_percpu_dynamic_enable(void);

/*
 * 动态分配清零后的 Per-CPU 变量（返回相对 %gs 的非零偏移指针，失败返回 NULL）：
 *   size  : 请求字节数（1 .. PCPU_MAX_ALLOC_SIZE）
 *   align : 对齐字节数（0 表示默认 8 B；非 0 必须为 2 的幂且 <= PCPU_MAX_ALLOC_ALIGN）
 * 注意：禁止在硬中断上下文调用。
 */
void *alloc_percpu_align(uint64 size, uint64 align);

/* 按类型分配便捷宏（__alignof__ 为 GCC 内建对齐操作符，用法同 sizeof） */
#define alloc_percpu(type) \
    ((type *)alloc_percpu_align((uint64)sizeof(type), (uint64)__alignof__(type)))

/* 释放动态 Per-CPU 变量（内置越界、未对齐、重复释放、野指针与静态区误释放拦截） */
void free_percpu(void *ptr);

/* 运行时统计快照结构体 */
typedef struct pcpu_stats_t {
    uint32 nr_cpus;
    uint32 huge_dir_end;       /* 大页覆盖的目录终点（0 表示 4 KB 降级模式） */
    uint32 first_dyn_dir;      /* 动态区起始目录号 */
    uint32 active_dirs;        /* 当前已激活（持有 4 KB 位图页）的目录数 */
    uint32 populated_4k_pages; /* 单核口径下 2 MB 之后已按需挂载的 4 KB 页数 */
    uint64 total_alloc_bytes;  /* 单核口径下当前活跃动态对象总字节数（8 B 对齐） */
} pcpu_stats_t;

void alloc_percpu_get_stats(pcpu_stats_t *out_stats);

/* =========================================================================
 * 4. 平台底层适配钩子（由宿主内核的 memblock / Buddy / VMM / SMP 提供）
 * ========================================================================= */

/* 查询逻辑 CPU 所属的逻辑 NUMA 节点 ID */
extern uint32 pcpu_arch_cpu_to_node(uint32 cpu_id);

/* 早期 memblock 按 NUMA 节点分配物理内存（返回物理地址，0 表示失败） */
extern uint64 pcpu_arch_memblock_alloc_node(uint64 size, uint64 align, uint32 node_id);

/* Buddy 按 NUMA 节点分配 1 张 4 KB (order-0) 物理页（返回物理地址，0 表示失败） */
extern uint64 pcpu_arch_buddy_alloc_page_node(uint32 node_id);

/* Buddy 归还 1 张 4 KB (order-0) 物理页 */
extern void pcpu_arch_buddy_free_page(uint64 phys_addr);

/* 物理地址 <-> 内核直接映射线性虚拟地址互转 */
extern void *pcpu_arch_phys_to_virt(uint64 phys_addr);
extern uint64 pcpu_arch_virt_to_phys(const void *virt_addr);

/* 页表映射：建立 2 MB PMD 大页映射（内核读写、不可执行、Global），成功返回 TRUE */
extern boolean pcpu_arch_vmm_map_2m(uint64 vaddr, uint64 phys_addr);

/* 页表映射：建立 4 KB PTE 映射（内核读写、不可执行、Global），成功返回 TRUE */
extern boolean pcpu_arch_vmm_map_4k(uint64 vaddr, uint64 phys_addr);

/* 解除 4 KB PTE 映射并返回原物理地址（仅改页表项，不发 IPI；未映射返回 0） */
extern uint64 pcpu_arch_vmm_unmap_4k(uint64 vaddr);

/* 向全系统所有在线 CPU 发起一次批量 TLB Shootdown IPI 刷新 */
extern void pcpu_arch_tlb_shootdown_all(void);

/* 同步锁接口（建议为互斥锁或不关本地中断的自旋锁，避免与 TLB Shootdown IPI 互锁） */
extern void pcpu_arch_lock(void);
extern void pcpu_arch_unlock(void);

/* 错误/告警日志回调 */
extern void pcpu_arch_log_warn(const char *msg, uint64 arg1, uint64 arg2);
