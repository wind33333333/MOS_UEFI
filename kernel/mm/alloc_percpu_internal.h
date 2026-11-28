#pragma once

#include "alloc_percpu.h"

/* =========================================================================
 * 两级懒加载双位图几何参数（严格咬合 4 KB 页与 64 B 缓存行边界）
 * ========================================================================= */

#define PCPU_PAGE_SHIFT          12U
#define PCPU_PAGE_SIZE           (1ULL << PCPU_PAGE_SHIFT)     /* 4 KB */
#define PCPU_HUGE_SHIFT          21U
#define PCPU_HUGE_SIZE           (1ULL << PCPU_HUGE_SHIFT)     /* 2 MB */

/* 每个二级位图页覆盖 128 KB Per-CPU 空间 */
#define PCPU_BMAP_SPAN_SHIFT     17U
#define PCPU_BMAP_SPAN_SIZE      (1U << PCPU_BMAP_SPAN_SHIFT)  /* 128 KB */
#define PCPU_BMAP_SPAN_MASK      (PCPU_BMAP_SPAN_SIZE - 1U)

/* 单张 4 KB 位图页：前 2 KB alloc_map + 后 2 KB bound_map = 精确 4096 B */
#define PCPU_BMAP_BITS           (PCPU_BMAP_SPAN_SIZE >> PCPU_MIN_ALLOC_SHIFT) /* 16384 bit */
#define PCPU_BMAP_WORDS          (PCPU_BMAP_BITS >> 6)                         /* 256 个 uint64 */

/* 每张 4 KB 数据页对应的位图大小：512 bit = 8 个 uint64 = 64 B（1 条缓存行） */
#define PCPU_BITS_PER_PAGE_SHIFT 9U
#define PCPU_BITS_PER_PAGE       (1U << PCPU_BITS_PER_PAGE_SHIFT)              /* 512 bit */
#define PCPU_WORDS_PER_PAGE      (PCPU_BITS_PER_PAGE >> 6)                     /* 8 words */
#define PCPU_PAGES_PER_DIR       (PCPU_BMAP_SPAN_SIZE >> PCPU_PAGE_SHIFT)      /* 32 页 */

/* 一级目录总数：128 MB / 128 KB = 1024 项，掩码仅占 16 个 uint64（2 条缓存行） */
#define PCPU_DIR_COUNT           ((uint32)(PERCPU_WINDOW_SIZE >> PCPU_BMAP_SPAN_SHIFT)) /* 1024 */
#define PCPU_MASK_WORDS          (PCPU_DIR_COUNT >> 6)                                  /* 16 */

/*
 * 二级位图页（严格 4096 字节，直接向 Buddy 申请 1 张 order-0 页）：
 *   alloc_map : 1 表示该 8 B 单元已分配，0 表示空闲
 *   bound_map : 1 表示该 8 B 单元是某个对象的末位，供 free_percpu O(1) 反查长度
 */
typedef struct pcpu_bmap_page_t {
    uint64 alloc_map[PCPU_BMAP_WORDS];
    uint64 bound_map[PCPU_BMAP_WORDS];
} pcpu_bmap_page_t;

/* 纯 C 编译期尺寸校验（不依赖 C11 _Static_assert，尺寸不对直接编译报错） */
typedef char pcpu_check_bmap_size[(sizeof(pcpu_bmap_page_t) == 4096) ? 1 : -1];
typedef char pcpu_check_win_size[(PERCPU_WINDOW_SIZE == (1ULL << 27)) ? 1 : -1];

/*
 * 一级目录项（24 字节，1024 项共 24 KB 常驻核心 .bss）：
 * 管理对应 128 KB 区间（含 32 张 4 KB 数据页）的挂载与游标状态。
 */
typedef struct pcpu_dir_entry_t {
    pcpu_bmap_page_t *bmap;          /* NULL 表示该 128 KB 区间未激活，占 0 字节位图内存 */
    uint32            populated_map; /* 32-bit：对应 32 张 4 KB 数据页是否已全核挂载 */
    uint16            free_bits;     /* 当前剩余空闲 8 B 单元数 (0 .. 16384) */
    uint16            first_free;    /* 首个空闲位下标（无空位时为 16384） */
    uint16            high_water;    /* 高水位游标：[high_water, 16384) 保证全空，支持 O(1) Bump */
    uint16            contig_hint;   /* 惰性最大连续空闲上界（永不低估，快速剪枝） */
} pcpu_dir_entry_t;

/*
 * 全局运行期状态（全部定义在 alloc_percpu.c 的常驻 .bss 中，
 * 避免放进 alloc_percpu_init.c 被 Kernel.lds 的 *init.o 规则丢弃！）
 */
extern pcpu_dir_entry_t g_pcpu_dir[PCPU_DIR_COUNT];
extern uint64           g_pcpu_avail_mask[PCPU_MASK_WORDS];  /* 已激活且有空位的目录掩码 */
extern uint64           g_pcpu_uninit_mask[PCPU_MASK_WORDS]; /* 未激活（bmap==NULL）的动态目录掩码 */
extern uint32           g_pcpu_first_dyn_dir;                /* 动态区首个合法目录号（恒 >= 1） */
extern uint32           g_pcpu_huge_dir_end;                 /* 前 2 MB 大页覆盖终点（0 表示 4 KB 降级） */
extern boolean          g_pcpu_early_ready;                  /* 阶段一（静态与 %gs）就绪标志 */
extern boolean          g_pcpu_dyn_ready;                    /* 阶段二（Buddy 就绪）动态分配开关 */

/* 运行时统计计数器（持锁更新） */
extern uint32           g_pcpu_stat_active_dirs;
extern uint32           g_pcpu_stat_pop_4k_pages;
extern uint64           g_pcpu_stat_alloc_bytes;

/* =========================================================================
 * 通用基础工具内联函数
 * ========================================================================= */

static inline uint64 pcpu_align_up_u64(uint64 val, uint64 align) {
    return (val + align - 1ULL) & ~(align - 1ULL);
}

/* 统计 64 位非零无符号整数最低位连续 0 的个数（uint64 即 unsigned long long） */
static inline uint32 pcpu_ctz64(uint64 x) {
    return (uint32)__builtin_ctzll(x);
}

static inline void pcpu_mask_set(uint64 *mask, uint32 idx) {
    mask[idx >> 6] |= (1ULL << (idx & 63U));
}

static inline void pcpu_mask_clear(uint64 *mask, uint32 idx) {
    mask[idx >> 6] &= ~(1ULL << (idx & 63U));
}

static inline void pcpu_memzero(void *dst, uint64 len) {
    uint8 *p = (uint8 *)dst;
    while (len >= 8ULL) {
        *(uint64 *)p = 0ULL;
        p += 8ULL;
        len -= 8ULL;
    }
    while (len > 0ULL) {
        *p++ = 0U;
        len--;
    }
}

static inline void pcpu_memcpy(void *dst, const void *src, uint64 len) {
    uint8 *d = (uint8 *)dst;
    const uint8 *s = (const uint8 *)src;
    while (len >= 8ULL) {
        *(uint64 *)d = *(const uint64 *)s;
        d += 8ULL;
        s += 8ULL;
        len -= 8ULL;
    }
    while (len > 0ULL) {
        *d++ = *s++;
        len--;
    }
}

/* 计算指定逻辑核在 Per-CPU 窗口中某字节偏移对应的全局规范虚拟地址 */
static inline uint64 pcpu_cpu_off_to_va(uint32 cpu_id, uint64 offset) {
    return g_pcpu_vaddr_base + (((uint64)cpu_id) << PERCPU_WINDOW_SHIFT) + offset;
}
