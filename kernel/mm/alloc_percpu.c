#include "alloc_percpu_internal.h"

/* 批量回收空页时的栈上暂存物理页帧容量（512 B 栈空间，零堆依赖，减少跨核 IPI 次数） */
#define PCPU_RECLAIM_BATCH_MAX  64U

/* =========================================================================
 * 0. 运行期常驻全局状态定义（位于核心 .bss，绝不被 .init_* 丢弃）
 * ========================================================================= */

uint64           g_pcpu_vaddr_base = 0ULL;
uint32           g_pcpu_nr_cpus    = 0U;

pcpu_dir_entry_t g_pcpu_dir[PCPU_DIR_COUNT];
uint64           g_pcpu_avail_mask[PCPU_MASK_WORDS];
uint64           g_pcpu_uninit_mask[PCPU_MASK_WORDS];

uint32           g_pcpu_first_dyn_dir = 1U;
uint32           g_pcpu_huge_dir_end  = 0U;
boolean          g_pcpu_early_ready   = FALSE;
boolean          g_pcpu_dyn_ready     = FALSE;

uint32           g_pcpu_stat_active_dirs  = 0U;
uint32           g_pcpu_stat_pop_4k_pages = 0U;
uint64           g_pcpu_stat_alloc_bytes  = 0ULL;

/*
 * 写入当前核心的 IA32_GS_BASE（放在常驻 .text 中，兼容后续 AP 热插拔唤醒）
 */
void alloc_percpu_setup_cpu_gs(uint32 cpu_id) {
    if (!g_pcpu_early_ready || cpu_id >= g_pcpu_nr_cpus) {
        return;
    }

    uint64 gs_base = pcpu_cpu_off_to_va(cpu_id, 0ULL);
    uint32 lo = (uint32)(gs_base & 0xFFFFFFFFULL);
    uint32 hi = (uint32)(gs_base >> 32);

    __asm__ __volatile__("wrmsr" :: "c"(MSR_IA32_GS_BASE), "a"(lo), "d"(hi) : "memory");
}

/* =========================================================================
 * 1. 基础位图原语（无未定义移位、字级跳过优化）
 * ========================================================================= */

static inline boolean bitmap_test_bit(const uint64 *map, uint32 bit) {
    return ((map[bit >> 6] & (1ULL << (bit & 63U))) != 0ULL) ? TRUE : FALSE;
}

static inline void bitmap_set_bit(uint64 *map, uint32 bit) {
    map[bit >> 6] |= (1ULL << (bit & 63U));
}

static inline void bitmap_clear_bit(uint64 *map, uint32 bit) {
    map[bit >> 6] &= ~(1ULL << (bit & 63U));
}

/* 批量置位 [start, start + count)，要求 count >= 1 */
static void bitmap_set_range(uint64 *map, uint32 start, uint32 count) {
    uint32 end_incl = start + count - 1U;
    uint32 w_start  = start >> 6;
    uint32 b_start  = start & 63U;
    uint32 w_end    = end_incl >> 6;
    uint32 b_end    = end_incl & 63U;
    uint32 w;

    if (w_start == w_end) {
        uint64 mask = (~0ULL >> (63U - (b_end - b_start))) << b_start;
        map[w_start] |= mask;
        return;
    }

    map[w_start] |= (~0ULL << b_start);
    for (w = w_start + 1U; w < w_end; w++) {
        map[w] = ~0ULL;
    }
    map[w_end] |= (~0ULL >> (63U - b_end));
}

/* 批量清零 [start, start + count)，要求 count >= 1 */
static void bitmap_clear_range(uint64 *map, uint32 start, uint32 count) {
    uint32 end_incl = start + count - 1U;
    uint32 w_start  = start >> 6;
    uint32 b_start  = start & 63U;
    uint32 w_end    = end_incl >> 6;
    uint32 b_end    = end_incl & 63U;
    uint32 w;

    if (w_start == w_end) {
        uint64 mask = (~0ULL >> (63U - (b_end - b_start))) << b_start;
        map[w_start] &= ~mask;
        return;
    }

    map[w_start] &= ~(~0ULL << b_start);
    for (w = w_start + 1U; w < w_end; w++) {
        map[w] = 0ULL;
    }
    map[w_end] &= ~(~0ULL >> (63U - b_end));
}

/* 在 [start, total_bits) 内查找下一个值为 0 的位，未找到返回 total_bits */
static uint32 bitmap_find_next_zero(const uint64 *map, uint32 total_bits, uint32 start) {
    if (start >= total_bits) {
        return total_bits;
    }
    uint32 w           = start >> 6;
    uint32 bit         = start & 63U;
    uint32 total_words = (total_bits + 63U) >> 6;
    uint64 inv         = (~map[w]) & (~0ULL << bit);

    while (inv == 0ULL) {
        w++;
        if (w >= total_words) {
            return total_bits;
        }
        inv = ~map[w];
    }

    uint32 res = (w << 6) + pcpu_ctz64(inv);
    return (res < total_bits) ? res : total_bits;
}

/* 在 [start, total_bits) 内查找下一个值为 1 的位，未找到返回 total_bits */
static uint32 bitmap_find_next_one(const uint64 *map, uint32 total_bits, uint32 start) {
    if (start >= total_bits) {
        return total_bits;
    }
    uint32 w           = start >> 6;
    uint32 bit         = start & 63U;
    uint32 total_words = (total_bits + 63U) >> 6;
    uint64 val         = map[w] & (~0ULL << bit);

    while (val == 0ULL) {
        w++;
        if (w >= total_words) {
            return total_bits;
        }
        val = map[w];
    }

    uint32 res = (w << 6) + pcpu_ctz64(val);
    return (res < total_bits) ? res : total_bits;
}

/* 在目录掩码 [start, end) 范围内查找下一个置 1 的目录号，未找到返回 end */
static uint32 mask_find_next_one_range(const uint64 *mask, uint32 start, uint32 end) {
    if (start >= end) {
        return end;
    }
    uint32 w      = start >> 6;
    uint32 bit    = start & 63U;
    uint32 last_w = (end - 1U) >> 6;
    uint64 val    = mask[w] & (~0ULL << bit);

    while (val == 0ULL) {
        w++;
        if (w > last_w) {
            return end;
        }
        val = mask[w];
    }

    uint32 res = (w << 6) + pcpu_ctz64(val);
    return (res < end) ? res : end;
}

/* 单缓存行（64 B = 8 个 uint64）无分支判空：检查某 4 KB 数据页内是否全部空闲 */
static inline boolean pcpu_page_is_empty(const pcpu_bmap_page_t *bmap, uint32 pg) {
    uint32 base = pg << 3;
    uint64 acc  = bmap->alloc_map[base + 0U] | bmap->alloc_map[base + 1U] |
                  bmap->alloc_map[base + 2U] | bmap->alloc_map[base + 3U] |
                  bmap->alloc_map[base + 4U] | bmap->alloc_map[base + 5U] |
                  bmap->alloc_map[base + 6U] | bmap->alloc_map[base + 7U];
    return (acc == 0ULL) ? TRUE : FALSE;
}

/* =========================================================================
 * 2. 二级位图页懒加载激活与释放（仅依赖 Buddy order-0 页）
 * ========================================================================= */

static boolean pcpu_activate_dir(uint32 dir_idx) {
    pcpu_dir_entry_t *dir = &g_pcpu_dir[dir_idx];

    /* 申请 1 张 4 KB order-0 Buddy 页存放 alloc_map(2KB) + bound_map(2KB) */
    uint64 phys = pcpu_arch_buddy_alloc_page_node(0U);
    if (phys == 0ULL) {
        return FALSE;
    }

    pcpu_bmap_page_t *bmap = (pcpu_bmap_page_t *)pcpu_arch_phys_to_virt(phys);
    pcpu_memzero(bmap, (uint64)sizeof(pcpu_bmap_page_t));

    /*
     * 严禁覆盖 dir->populated_map：
     * 前 2 MB 大页区内的目录即便排空释放了位图页，其 populated_map 仍为全 1。
     */
    dir->bmap        = bmap;
    dir->free_bits   = (uint16)PCPU_BMAP_BITS;
    dir->first_free  = 0U;
    dir->high_water  = 0U;
    dir->contig_hint = (uint16)PCPU_BMAP_BITS;

    pcpu_mask_clear(g_pcpu_uninit_mask, dir_idx);
    pcpu_mask_set(g_pcpu_avail_mask, dir_idx);
    g_pcpu_stat_active_dirs++;
    return TRUE;
}

static void pcpu_deactivate_dir(uint32 dir_idx) {
    pcpu_dir_entry_t *dir = &g_pcpu_dir[dir_idx];
    if (dir->bmap == NULL) {
        return;
    }

    uint64 bmap_phys = pcpu_arch_virt_to_phys(dir->bmap);
    dir->bmap        = NULL;
    dir->free_bits   = 0U;
    dir->first_free  = 0U;
    dir->high_water  = 0U;
    dir->contig_hint = 0U;

    pcpu_mask_clear(g_pcpu_avail_mask, dir_idx);
    pcpu_mask_set(g_pcpu_uninit_mask, dir_idx);
    g_pcpu_stat_active_dirs--;

    pcpu_arch_buddy_free_page(bmap_phys);
}

/* =========================================================================
 * 3. 4 KB 数据页按需 NUMA 挂载（含完整 OOM 回滚）与批量回收
 * ========================================================================= */

/* 回滚尚未暴露给任何调用者的临时映射页，防止部分成功导致内存泄漏 */
static void pcpu_rollback_pages(uint32 dir_idx, uint32 rollback_mask,
                                uint32 partial_pg, uint32 partial_cpu_end) {
    uint64 dir_base_off = ((uint64)dir_idx) << PCPU_BMAP_SPAN_SHIFT;
    uint32 cpu;

    /* 1. 回滚当前失败页上已经分配映射的部分 CPU 副本 */
    if (partial_cpu_end > 0U) {
        uint64 pg_off = dir_base_off + (((uint64)partial_pg) << PCPU_PAGE_SHIFT);
        for (cpu = 0U; cpu < partial_cpu_end; cpu++) {
            uint64 phys = pcpu_arch_vmm_unmap_4k(pcpu_cpu_off_to_va(cpu, pg_off));
            if (phys != 0ULL) {
                pcpu_arch_buddy_free_page(phys);
            }
        }
    }

    /* 2. 回滚本次调用中先前已全核挂载成功的整页 */
    while (rollback_mask != 0U) {
        uint32 pg = pcpu_ctz64((uint64)rollback_mask);
        rollback_mask &= (rollback_mask - 1U);
        uint64 pg_off = dir_base_off + (((uint64)pg) << PCPU_PAGE_SHIFT);

        for (cpu = 0U; cpu < g_pcpu_nr_cpus; cpu++) {
            uint64 phys = pcpu_arch_vmm_unmap_4k(pcpu_cpu_off_to_va(cpu, pg_off));
            if (phys != 0ULL) {
                pcpu_arch_buddy_free_page(phys);
            }
        }
        g_pcpu_stat_pop_4k_pages--;
    }
}

/* 按需为 [pg_start, pg_end] 中尚未挂载的 4 KB 页向每核本地 NUMA 批页并映射 */
static boolean pcpu_populate_range(uint32 dir_idx, uint32 pg_start, uint32 pg_end) {
    pcpu_dir_entry_t *dir = &g_pcpu_dir[dir_idx];

    /* 构造 [pg_start, pg_end] 的 32 位页掩码，筛出缺失页 */
    uint32 cnt          = (pg_end - pg_start) + 1U;
    uint32 range_mask   = (cnt == 32U) ? 0xFFFFFFFFU : (((1U << cnt) - 1U) << pg_start);
    uint32 missing_mask = range_mask & (~dir->populated_map);

    if (missing_mask == 0U) {
        /* 命中前 2 MB 大页或已挂载热页：零开销直接返回 */
        return TRUE;
    }

    uint64 dir_base_off   = ((uint64)dir_idx) << PCPU_BMAP_SPAN_SHIFT;
    uint32 newly_pop_mask = 0U;

    while (missing_mask != 0U) {
        uint32 pg = pcpu_ctz64((uint64)missing_mask);
        missing_mask &= (missing_mask - 1U);
        uint64 pg_off = dir_base_off + (((uint64)pg) << PCPU_PAGE_SHIFT);
        uint32 cpu;

        for (cpu = 0U; cpu < g_pcpu_nr_cpus; cpu++) {
            uint32 node = pcpu_arch_cpu_to_node(cpu);
            uint64 phys = pcpu_arch_buddy_alloc_page_node(node);
            if (phys == 0ULL) {
                pcpu_rollback_pages(dir_idx, newly_pop_mask, pg, cpu);
                return FALSE;
            }

            /* 映射前先经内核直接映射清零整页 */
            pcpu_memzero(pcpu_arch_phys_to_virt(phys), PCPU_PAGE_SIZE);

            uint64 vaddr = pcpu_cpu_off_to_va(cpu, pg_off);
            if (!pcpu_arch_vmm_map_4k(vaddr, phys)) {
                pcpu_arch_buddy_free_page(phys);
                pcpu_rollback_pages(dir_idx, newly_pop_mask, pg, cpu);
                return FALSE;
            }
        }

        newly_pop_mask |= (1U << pg);
        g_pcpu_stat_pop_4k_pages++;
    }

    dir->populated_map |= newly_pop_mask;
    return TRUE;
}

/* 批量解除空页映射 -> 发一次跨核 TLB Shootdown IPI -> 归还 Buddy（严守硬件时序） */
static void pcpu_unpopulate_pages(uint32 dir_idx, uint32 unmap_mask) {
    uint64 dir_base_off = ((uint64)dir_idx) << PCPU_BMAP_SPAN_SHIFT;
    uint64 phys_batch[PCPU_RECLAIM_BATCH_MAX];
    uint32 batch_cnt = 0U;
    uint32 i;

    while (unmap_mask != 0U) {
        uint32 pg = pcpu_ctz64((uint64)unmap_mask);
        unmap_mask &= (unmap_mask - 1U);
        uint64 pg_off = dir_base_off + (((uint64)pg) << PCPU_PAGE_SHIFT);
        uint32 cpu;

        for (cpu = 0U; cpu < g_pcpu_nr_cpus; cpu++) {
            uint64 phys = pcpu_arch_vmm_unmap_4k(pcpu_cpu_off_to_va(cpu, pg_off));
            if (phys != 0ULL) {
                phys_batch[batch_cnt++] = phys;

                /* 满 64 张时统一刷一次全核 TLB 后再批量归还 Buddy */
                if (batch_cnt == PCPU_RECLAIM_BATCH_MAX) {
                    pcpu_arch_tlb_shootdown_all();
                    for (i = 0U; i < batch_cnt; i++) {
                        pcpu_arch_buddy_free_page(phys_batch[i]);
                    }
                    batch_cnt = 0U;
                }
            }
        }

        g_pcpu_stat_pop_4k_pages--;
    }

    /* 尾批处理：确保所有核心 TLB 失效后再把物理页放回 Buddy */
    if (batch_cnt > 0U) {
        pcpu_arch_tlb_shootdown_all();
        for (i = 0U; i < batch_cnt; i++) {
            pcpu_arch_buddy_free_page(phys_batch[i]);
        }
    }
}

/* =========================================================================
 * 4. 单个目录内搜索与提交分配逻辑
 * ========================================================================= */

/*
 * 在已激活目录内查找满足对齐和长度要求的连续空闲位：
 * 找到返回 TRUE 并写出 *out_pos；找不到返回 FALSE（并在全扫后收紧 contig_hint）。
 */
static boolean pcpu_find_in_dir(pcpu_dir_entry_t *dir,
                                uint32 need_bits,
                                uint32 align_bits,
                                uint32 *out_pos) {
    /* 惰性剪枝：余量或上界装不下时零访存直接跳过 */
    if ((uint32)dir->free_bits < need_bits || (uint32)dir->contig_hint < need_bits) {
        return FALSE;
    }

    uint32 align_mask = align_bits - 1U;

    /*
     * O(1) Bump 快路径：
     * first_free == high_water 说明高水位下方无任何空洞，
     * 直接在高水位处对齐切分，无需读取 4 KB 二级位图页。
     */
    if (dir->first_free == dir->high_water) {
        uint32 aligned_pos = ((uint32)dir->high_water + align_mask) & (~align_mask);
        if (aligned_pos + need_bits <= PCPU_BMAP_BITS) {
            *out_pos = aligned_pos;
            return TRUE;
        }
        return FALSE;
    }

    /* 常规低地址优先扫描：遍历所有连续 0 区间，顺带测量精确最大连续空洞 */
    const uint64 *alloc_map = dir->bmap->alloc_map;
    uint32 pos     = (uint32)dir->first_free;
    uint32 max_run = 0U;

    while (pos < PCPU_BMAP_BITS) {
        uint32 run_start = bitmap_find_next_zero(alloc_map, PCPU_BMAP_BITS, pos);
        if (run_start >= PCPU_BMAP_BITS) {
            break;
        }
        uint32 run_end = bitmap_find_next_one(alloc_map, PCPU_BMAP_BITS, run_start);
        uint32 run_len = run_end - run_start;
        if (run_len > max_run) {
            max_run = run_len;
        }

        uint32 aligned_start = (run_start + align_mask) & (~align_mask);
        if (aligned_start + need_bits <= run_end) {
            *out_pos = aligned_start;
            return TRUE;
        }
        pos = run_end;
    }

    /* 全扫未命中：将 contig_hint 精确收紧为真实最大连续空洞长度 */
    dir->contig_hint = (uint16)max_run;
    return FALSE;
}

/* 在目录 dir_idx 的 alloc_pos 处执行挂载、清零与双位图提交 */
static boolean pcpu_commit_in_dir(uint32 dir_idx, uint32 alloc_pos, uint32 need_bits) {
    pcpu_dir_entry_t *dir  = &g_pcpu_dir[dir_idx];
    pcpu_bmap_page_t *bmap = dir->bmap;

    uint32 end_bit_incl = alloc_pos + need_bits - 1U;
    uint32 pg_start     = alloc_pos >> PCPU_BITS_PER_PAGE_SHIFT;
    uint32 pg_end       = end_bit_incl >> PCPU_BITS_PER_PAGE_SHIFT;

    /* 第一步：保证所跨 4 KB 物理页全部按本地 NUMA 挂载就绪（失败自动回滚） */
    if (!pcpu_populate_range(dir_idx, pg_start, pg_end)) {
        return FALSE;
    }

    /* 第二步：全核清零新分配的对象区间（确保复用旧空洞与大页槽位均为全 0） */
    uint64 obj_off   = (((uint64)dir_idx) << PCPU_BMAP_SPAN_SHIFT) +
                       (((uint64)alloc_pos) << PCPU_MIN_ALLOC_SHIFT);
    uint64 obj_bytes = ((uint64)need_bits) << PCPU_MIN_ALLOC_SHIFT;
    uint32 cpu;
    for (cpu = 0U; cpu < g_pcpu_nr_cpus; cpu++) {
        pcpu_memzero((void *)pcpu_cpu_off_to_va(cpu, obj_off), obj_bytes);
    }

    /* 第三步：提交双位图（alloc_map 置满区间，bound_map 仅置末位） */
    bitmap_set_range(bmap->alloc_map, alloc_pos, need_bits);
    bitmap_set_bit(bmap->bound_map, end_bit_incl);

    /* 第四步：维护目录摘要游标 */
    dir->free_bits = (uint16)(dir->free_bits - (uint16)need_bits);
    if (alloc_pos + need_bits > (uint32)dir->high_water) {
        dir->high_water = (uint16)(alloc_pos + need_bits);
    }

    if (dir->free_bits == 0U) {
        dir->first_free  = (uint16)PCPU_BMAP_BITS;
        dir->contig_hint = 0U;
        pcpu_mask_clear(g_pcpu_avail_mask, dir_idx);
    } else {
        if (alloc_pos == (uint32)dir->first_free) {
            dir->first_free = (uint16)bitmap_find_next_zero(bmap->alloc_map,
                                                            PCPU_BMAP_BITS,
                                                            alloc_pos + need_bits);
        }
        if (dir->first_free == dir->high_water) {
            dir->contig_hint = (uint16)(PCPU_BMAP_BITS - dir->high_water);
        } else if (dir->contig_hint > dir->free_bits) {
            dir->contig_hint = dir->free_bits;
        }
    }

    g_pcpu_stat_alloc_bytes += obj_bytes;
    return TRUE;
}

/* 尝试在 [dir_begin, dir_end) 内已激活目录中完成分配 */
static uint64 pcpu_try_active_range(uint32 dir_begin, uint32 dir_end,
                                    uint32 need_bits, uint32 align_bits) {
    uint32 dir_idx = dir_begin;
    while (dir_idx < dir_end) {
        dir_idx = mask_find_next_one_range(g_pcpu_avail_mask, dir_idx, dir_end);
        if (dir_idx >= dir_end) {
            break;
        }

        uint32 alloc_pos = 0U;
        if (pcpu_find_in_dir(&g_pcpu_dir[dir_idx], need_bits, align_bits, &alloc_pos)) {
            if (!pcpu_commit_in_dir(dir_idx, alloc_pos, need_bits)) {
                return 0ULL;
            }
            return (((uint64)dir_idx) << PCPU_BMAP_SPAN_SHIFT) +
                   (((uint64)alloc_pos) << PCPU_MIN_ALLOC_SHIFT);
        }

        dir_idx++;
    }
    return 0ULL;
}

/* 尝试在 [dir_begin, dir_end) 内懒加载激活首个未激活目录并完成分配 */
static uint64 pcpu_try_uninit_range(uint32 dir_begin, uint32 dir_end, uint32 need_bits) {
    uint32 dir_idx = mask_find_next_one_range(g_pcpu_uninit_mask, dir_begin, dir_end);
    if (dir_idx >= dir_end) {
        return 0ULL;
    }

    if (!pcpu_activate_dir(dir_idx)) {
        return 0ULL;
    }

    /* 新激活目录的起始偏移 0 天然满足任意 2 的幂对齐（<= 128 KB） */
    if (!pcpu_commit_in_dir(dir_idx, 0U, need_bits)) {
        /* 物理数据页挂载失败：顺手释放刚申请的 4 KB 位图页，确保零泄漏 */
        pcpu_deactivate_dir(dir_idx);
        return 0ULL;
    }

    return ((uint64)dir_idx) << PCPU_BMAP_SPAN_SHIFT;
}

/* =========================================================================
 * 5. 对外核心分配、释放与统计接口
 * ========================================================================= */

void *alloc_percpu_align(uint64 size, uint64 align) {
    if (!g_pcpu_dyn_ready || size == 0ULL) {
        return NULL;
    }

    /* 对齐合法性校验与规整 */
    if (align == 0ULL) {
        align = (uint64)PCPU_MIN_ALLOC_SIZE;
    }
    if ((align & (align - 1ULL)) != 0ULL || align > (uint64)PCPU_MAX_ALLOC_ALIGN) {
        return NULL;
    }
    if (align < (uint64)PCPU_MIN_ALLOC_SIZE) {
        align = (uint64)PCPU_MIN_ALLOC_SIZE;
    }

    uint64 size_aligned = pcpu_align_up_u64(size, (uint64)PCPU_MIN_ALLOC_SIZE);
    if (size_aligned > (uint64)PCPU_MAX_ALLOC_SIZE) {
        return NULL;
    }

    uint32 need_bits  = (uint32)(size_aligned >> PCPU_MIN_ALLOC_SHIFT);
    uint32 align_bits = (uint32)(align >> PCPU_MIN_ALLOC_SHIFT);
    uint64 result_off = 0ULL;

    pcpu_arch_lock();

    /*
     * 两段式低地址强聚合路由：
     * 优先填满前 2 MB 热点 PMD 大页区 [first_dyn_dir, huge_dir_end)，
     * 只有前 2 MB 装不下时，才溢出到 [huge_dir_end, 1024) 的 4 KB 按需挂载区。
     */
    uint32 huge_split = (g_pcpu_huge_dir_end > g_pcpu_first_dyn_dir)
                            ? g_pcpu_huge_dir_end
                            : g_pcpu_first_dyn_dir;

    if (huge_split > g_pcpu_first_dyn_dir) {
        result_off = pcpu_try_active_range(g_pcpu_first_dyn_dir, huge_split,
                                           need_bits, align_bits);
        if (result_off == 0ULL) {
            result_off = pcpu_try_uninit_range(g_pcpu_first_dyn_dir, huge_split,
                                               need_bits);
        }
    }

    if (result_off == 0ULL) {
        result_off = pcpu_try_active_range(huge_split, PCPU_DIR_COUNT,
                                           need_bits, align_bits);
    }
    if (result_off == 0ULL) {
        result_off = pcpu_try_uninit_range(huge_split, PCPU_DIR_COUNT,
                                           need_bits);
    }

    pcpu_arch_unlock();
    return (void *)result_off;
}

void free_percpu(void *ptr) {
    if (ptr == NULL || !g_pcpu_dyn_ready) {
        return;
    }

    uint64 off = (uint64)ptr;

    /* 1. O(1) 基础合法性拦截：越界、非 8 B 对齐、误释放静态区 */
    if (off >= PERCPU_WINDOW_SIZE || (off & ((uint64)PCPU_MIN_ALLOC_SIZE - 1ULL)) != 0ULL) {
        pcpu_arch_log_warn("percpu: free invalid/unaligned ptr", off, 0ULL);
        return;
    }

    uint32 dir_idx = (uint32)(off >> PCPU_BMAP_SPAN_SHIFT);
    if (dir_idx < g_pcpu_first_dyn_dir) {
        pcpu_arch_log_warn("percpu: attempt to free static percpu var", off, (uint64)dir_idx);
        return;
    }

    uint32 bit_start = (uint32)((off & (uint64)PCPU_BMAP_SPAN_MASK) >> PCPU_MIN_ALLOC_SHIFT);

    pcpu_arch_lock();

    pcpu_dir_entry_t *dir = &g_pcpu_dir[dir_idx];
    if (dir->bmap == NULL) {
        pcpu_arch_log_warn("percpu: free in unactivated dir", off, (uint64)dir_idx);
        pcpu_arch_unlock();
        return;
    }

    pcpu_bmap_page_t *bmap = dir->bmap;

    /* 2. 带外安全校验：拦截重复释放（Double-Free） */
    if (!bitmap_test_bit(bmap->alloc_map, bit_start)) {
        pcpu_arch_log_warn("percpu: double free detected", off, (uint64)bit_start);
        pcpu_arch_unlock();
        return;
    }

    /* 3. 带外安全校验：拦截指向对象内部的野指针 */
    if (bit_start > 0U &&
        bitmap_test_bit(bmap->alloc_map, bit_start - 1U) &&
        !bitmap_test_bit(bmap->bound_map, bit_start - 1U)) {
        pcpu_arch_log_warn("percpu: interior wild pointer free detected", off, (uint64)bit_start);
        pcpu_arch_unlock();
        return;
    }

    /* 4. O(1) 查 bound_map 定位对象末位并精确还原长度 */
    uint32 bit_end_incl = bitmap_find_next_one(bmap->bound_map, PCPU_BMAP_BITS, bit_start);
    if (bit_end_incl >= PCPU_BMAP_BITS) {
        pcpu_arch_log_warn("percpu: bound_map corrupted", off, (uint64)bit_start);
        pcpu_arch_unlock();
        return;
    }

    uint32 bit_count = (bit_end_incl - bit_start) + 1U;

    /* 5. 清零双位图并恢复目录游标 */
    bitmap_clear_range(bmap->alloc_map, bit_start, bit_count);
    bitmap_clear_bit(bmap->bound_map, bit_end_incl);

    dir->free_bits = (uint16)(dir->free_bits + (uint16)bit_count);
    if (bit_start < (uint32)dir->first_free) {
        dir->first_free = (uint16)bit_start;
    }
    if ((bit_end_incl + 1U) == (uint32)dir->high_water) {
        dir->high_water = (uint16)bit_start;
    }
    /* 保守放大上界提示，后续若遇碎片未命中会自动收紧至精确值 */
    dir->contig_hint = dir->free_bits;
    pcpu_mask_set(g_pcpu_avail_mask, dir_idx);

    g_pcpu_stat_alloc_bytes -= (((uint64)bit_count) << PCPU_MIN_ALLOC_SHIFT);

    /* 6. 仅对 2 MB 之后的 4 KB 按需挂载区执行单缓存行空页检测与回收 */
    if (dir_idx >= g_pcpu_huge_dir_end) {
        uint32 pg_start   = bit_start >> PCPU_BITS_PER_PAGE_SHIFT;
        uint32 pg_end     = bit_end_incl >> PCPU_BITS_PER_PAGE_SHIFT;
        uint32 unmap_mask = 0U;
        uint32 pg;

        for (pg = pg_start; pg <= pg_end; pg++) {
            if ((dir->populated_map & (1U << pg)) != 0U && pcpu_page_is_empty(bmap, pg)) {
                unmap_mask |= (1U << pg);
            }
        }

        if (unmap_mask != 0U) {
            dir->populated_map &= (~unmap_mask);
            pcpu_unpopulate_pages(dir_idx, unmap_mask);
        }
    }

    /* 7. 整个 128 KB 目录排空后，将 4 KB 二级位图页本身也归还 Buddy */
    if (dir->free_bits == (uint16)PCPU_BMAP_BITS) {
        pcpu_deactivate_dir(dir_idx);
    }

    pcpu_arch_unlock();
}

void alloc_percpu_get_stats(pcpu_stats_t *out_stats) {
    if (out_stats == NULL) {
        return;
    }

    pcpu_arch_lock();
    out_stats->nr_cpus            = g_pcpu_nr_cpus;
    out_stats->huge_dir_end       = g_pcpu_huge_dir_end;
    out_stats->first_dyn_dir      = g_pcpu_first_dyn_dir;
    out_stats->active_dirs        = g_pcpu_stat_active_dirs;
    out_stats->populated_4k_pages = g_pcpu_stat_pop_4k_pages;
    out_stats->total_alloc_bytes  = g_pcpu_stat_alloc_bytes;
    pcpu_arch_unlock();
}