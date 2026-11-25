#pragma once
#include "moslib.h"
#include "list.h"
#include "vmm.h"

/* =========================================================================
 * Per-CPU 无限扩容虚拟布局与双位图常量定义
 * ========================================================================= */

/* 单个 Chunk 每核虚拟容量：256 KB (2^18 字节 = 64 个 4KB 页)
 * 选型依据：
 *   1) 64 页刚好对应单个 uint64 位图 (populated_map / pinned_map)，单寄存器位运算效率最高；
 *   2) 配合 8B 粒度双位图，整块 Chunk 控制元数据仅约 8.8 KB，可由 kmalloc 直接承载；
 *   3) 轻松容纳 cpu_control_block_t 及启动早期对象，且在按页按需挂载下物理内存零浪费。
 */
#define PCPU_CHUNK_SHIFT            18U
#define PCPU_CHUNK_SIZE             (1ULL << PCPU_CHUNK_SHIFT)

/* 核间步长严格等于单个 Chunk 的每核容量：
 * 保证任意 vmalloc Chunk 内相邻 CPU 的虚拟地址差恒为 PCPU_CHUNK_SIZE，
 * 从而在 64 位 %gs 寻址中精确抵消核号偏移，实现无限 Chunk 扩展。
 */
#define PERCPU_WINDOW_SIZE          PCPU_CHUNK_SIZE

/* 双位图最小分配粒度：8 字节 (匹配 x86-64 自然对齐) */
#define PCPU_MIN_ALLOC_SHIFT        3U
#define PCPU_MIN_ALLOC_SIZE         (1U << PCPU_MIN_ALLOC_SHIFT)

/* 单 Chunk 位图维度：256KB / 8B = 32768 bit = 512 个 uint64 字 (单张位图严格占 4KB) */
#define PCPU_CHUNK_BITS             ((uint32)(PCPU_CHUNK_SIZE >> PCPU_MIN_ALLOC_SHIFT))
#define PCPU_CHUNK_WORDS            (PCPU_CHUNK_BITS >> 6)

/* 单 Chunk 页级维度：256KB / 4KB = 64 页，每页 512 bit (8 个 uint64 字) */
#define PCPU_PAGE_SHIFT             12U
#define PCPU_PAGE_SIZE              (1U << PCPU_PAGE_SHIFT)
#define PCPU_CHUNK_PAGES            ((uint32)(PCPU_CHUNK_SIZE >> PCPU_PAGE_SHIFT))
#define PCPU_BITS_PER_PAGE_SHIFT    9U
#define PCPU_BITS_PER_PAGE          (1U << PCPU_BITS_PER_PAGE_SHIFT)
#define PCPU_WORDS_PER_PAGE         (PCPU_BITS_PER_PAGE >> 6)

/* 创世 Chunk 0 早期预映射下限：16 页 (64 KB)，确保 Buddy 未完全就绪前有足够零分配缓冲 */
#define PCPU_EARLY_PREMAP_PAGES     16U

/* =========================================================================
 * 页级空闲摘要 (8 字节/页，64 页合计 512 字节)
 * 用于首次适应 O(1) 跳页剪枝、跨页连续块拼接，以及空页物理回收判定
 * ========================================================================= */
typedef struct pcpu_page_md_t {
    uint16 first_free;      /* 本页首个空闲位偏移 (0..511；512 表示整页已满) */
    uint16 contig_hint;     /* 本页最大连续空闲位数 (0..512；512 表示整页全空可回收物理页) */
    uint16 left_free;       /* 从本页 bit 0 起向后延伸的连续空闲位数 (跨页拼接左端) */
    uint16 right_free;      /* 截至本页 bit 511 向前延伸的连续空闲位数 (跨页拼接右端) */
} pcpu_page_md_t;

/* =========================================================================
 * Chunk 控制块 (内嵌双位图 + 64 页挂载位图，整体约 8.8 KB，无二级块链表)
 * 虚拟布局：
 *   本 Chunk 占用连续虚拟区间 [base_va, base_va + nr_possible_cpus * PCPU_CHUNK_SIZE)
 *   其中第 i 颗逻辑核心的切片为 [base_va + i * PCPU_CHUNK_SIZE, base_va + (i+1) * PCPU_CHUNK_SIZE)
 * 双位图语义：
 *   - alloc_map[b] == 1 : 第 b 个 8B 单元已被占用；0 表示空闲
 *   - bound_map[b] == 1 : 第 b 个 8B 单元是某个已分配对象的【最后一个单元】
 * ========================================================================= */
typedef struct pcpu_chunk_t {
    list_head_t    node;                        /* 挂载至全局 chunk_list (无限动态增长) */
    uint64         base_va;                     /* 本 Chunk CPU 0 切片的起始虚拟地址 */
    uint32         free_bits;                   /* 本 Chunk 剩余总空闲单元数 (单位：8B) */
    uint32         first_free;                  /* 本 Chunk 首个空闲位索引 (首次适应快速起点) */
    uint32         contig_hint;                 /* 本 Chunk 精确最大连续空闲位数 (含跨页拼接) */
    uint32         _pad;                        /* 8 字节对齐填充 */
    uint64         populated_map;               /* 64-bit 物理页挂载位图：bit p == 1 表示第 p 页已对全核映射 */
    uint64         pinned_map;                  /* 64-bit 固定页位图：memblock 早期映射页置 1，禁止归还 Buddy */
    pcpu_page_md_t page_md[PCPU_CHUNK_PAGES];   /* 64 个 4KB 逻辑页的空闲摘要表 (512 B) */
    uint64         alloc_map[PCPU_CHUNK_WORDS]; /* 占用位图 (4 KB) */
    uint64         bound_map[PCPU_CHUNK_WORDS]; /* 块末位边界位图 (4 KB) */
} pcpu_chunk_t;

/**
 * 跨核访问：通过 64 位模加运算，获取目标逻辑核心的私有变量真实线性地址
 * 数学恒等式：
 *   base_0 + ((base_k - base_0) + in_chunk_off) + lid * S == base_k + lid * S + in_chunk_off
 * @param ptr        alloc_percpu 返回的相对 %gs 偏移量指针
 * @param logical_id 目标 CPU 逻辑核心编号
 * @return           目标 CPU 该变量的真实内核虚拟地址
 */
static inline void *per_cpu_ptr(const void *ptr, uint32 logical_id) {
    uint64 va = vm_layout.percpu_vaddr_start +
                (uint64)ptr +
                ((uint64)logical_id * PERCPU_WINDOW_SIZE);
    return (void *)va;
}

/* 早期启动初始化入口 (位于 alloc_percpu_init.c，通过 memblock 预映射创世 Chunk 0 头部页) */
void alloc_percpu_init(void);

/* 由 alloc_percpu_init() 调用：初始化创世 Chunk 0 双位图、固定页掩码及 CCB 保留区 */
void pcpu_first_chunk_setup(uint64 base_va, uint32 reserved_bytes, uint32 premapped_pages);

/* 动态 Per-CPU 内存分配与释放 (返回相对 %gs 偏移量指针，支持无限 vmalloc 扩容与按页按需挂载) */
void *alloc_percpu(uint32 size, uint32 align);
void  free_percpu(void *ptr);