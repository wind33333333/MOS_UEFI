#include "alloc_percpu.h"
#include "../x64/cpu.h"
#include "../x64/numa_node.h"
#include "pmm.h"
#include "printk.h"
#include "slub.h"
#include "list.h"

extern vm_space_t kernel_space;

/* 将任意位索引 x 向上对齐到满足 (x % align_bits == align_phase) 的最小位置 */
#define ALIGN_UP_PHASE(x) ((((uint32)(x) + align_mask - align_phase) & ~align_mask) + align_phase)

/* =========================================================================
 * vmalloc 纯虚拟地址区间预留/释放桥接钩子 (仅分配虚拟地址，不立即挂物理页)
 * ⚠️ 接入说明：
 *   请将下面两处声明替换为你内核 vmalloc 子系统中“仅保留虚拟地址区间”的实际接口
 *   (例如 vmm_alloc_vaddr / vm_area_alloc 等，要求返回 4KB 对齐的内核虚拟地址)。
 *   注意：不要直接调用“会立即分配并映射物理页”的常规 vmalloc()，否则会破坏
 *   按页按需挂载 (Populate-on-Alloc) 与各核本地 NUMA 物理页绑定。
 * ========================================================================= */
extern uint64 vmalloc_reserve_vaddr(uint64 size);
extern void   vmalloc_release_vaddr(uint64 va, uint64 size);

static inline uint64 pcpu_vm_reserve_range(uint64 total_bytes) {
    return vmalloc_reserve_vaddr(total_bytes);
}

static inline void pcpu_vm_release_range(uint64 base_va, uint64 total_bytes) {
    vmalloc_release_vaddr(base_va, total_bytes);
}

/* =========================================================================
 * 全局 Per-CPU 分配器状态 (彻底移除固定 256-Chunk 上限，由 chunk_list 无限扩展)
 * ========================================================================= */
static list_head_t  chunk_list;                 /* 所有活跃 Chunk 链表 (含创世 Chunk 0 及全部 vmalloc Chunk) */
static uint32       pcpu_alloc_lock = 0;        /* Per-CPU 分配/释放/挂载全局自旋锁 */

static pcpu_chunk_t first_chunk;                /* 创世 Chunk 0 (静态常驻，永不回收) */
static uint32       first_chunk_reserved_bits;  /* Chunk 0 头部永久保留给 CCB 的 8B 单元数 */

/* =========================================================================
 * 一、底层位图操作原语 (纯 64 位字长操作 + 硬件 TZCNT，严格规避向 tzcnt 传 0)
 * ========================================================================= */

/* 将任意正整数向上取整至最近的 2 的幂 (用于规范化 align 参数) */
static inline uint32 pcpu_roundup_pow2(uint32 v) {
    if (v <= 1U) {
        return 1U;
    }
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1U;
}

/* 构造覆盖逻辑页区间 [first_page, last_page] 的 64 位掩码 (0 <= first <= last < 64) */
static inline uint64 pcpu_page_range_mask(uint32 first_page, uint32 last_page) {
    uint32 count = last_page - first_page + 1U;
    uint64 mask  = (count >= 64U) ? ~0ULL : ((1ULL << count) - 1ULL);
    return mask << first_page;
}

static inline boolean pcpu_bitmap_test(const uint64 *bitmap, uint32 bit) {
    return ((bitmap[bit >> 6] & (1ULL << (bit & 63U))) != 0ULL) ? TRUE : FALSE;
}

static inline void pcpu_bitmap_set_bit(uint64 *bitmap, uint32 bit) {
    bitmap[bit >> 6] |= (1ULL << (bit & 63U));
}

static inline void pcpu_bitmap_clear_bit(uint64 *bitmap, uint32 bit) {
    bitmap[bit >> 6] &= ~(1ULL << (bit & 63U));
}

/* 批量置位 [start, start + count) */
static void pcpu_bitmap_set_range(uint64 *bitmap, uint32 start, uint32 count) {
    uint32 bit = start;
    uint32 end = start + count;

    while (bit < end) {
        uint32 word_idx = bit >> 6;
        uint32 bit_off  = bit & 63U;
        uint32 remain   = end - bit;
        uint32 take     = (remain < (64U - bit_off)) ? remain : (64U - bit_off);
        uint64 mask     = (take == 64U) ? ~0ULL : (((1ULL << take) - 1ULL) << bit_off);

        bitmap[word_idx] |= mask;
        bit += take;
    }
}

/* 批量清零 [start, start + count) */
static void pcpu_bitmap_clear_range(uint64 *bitmap, uint32 start, uint32 count) {
    uint32 bit = start;
    uint32 end = start + count;

    while (bit < end) {
        uint32 word_idx = bit >> 6;
        uint32 bit_off  = bit & 63U;
        uint32 remain   = end - bit;
        uint32 take     = (remain < (64U - bit_off)) ? remain : (64U - bit_off);
        uint64 mask     = (take == 64U) ? ~0ULL : (((1ULL << take) - 1ULL) << bit_off);

        bitmap[word_idx] &= ~mask;
        bit += take;
    }
}

/* 在区间 [start, start + count) 内查找首个为 1 的位索引；全为 0 返回 -1 */
static int32 pcpu_bitmap_find_first_set(const uint64 *bitmap, uint32 start, uint32 count) {
    uint32 bit = start;
    uint32 end = start + count;

    while (bit < end) {
        uint32 word_idx = bit >> 6;
        uint32 bit_off  = bit & 63U;
        uint32 remain   = end - bit;
        uint32 take     = (remain < (64U - bit_off)) ? remain : (64U - bit_off);
        uint64 mask     = (take == 64U) ? ~0ULL : (((1ULL << take) - 1ULL) << bit_off);

        uint64 hit = bitmap[word_idx] & mask;
        if (hit != 0ULL) {
            return (int32)((word_idx << 6) + (uint32)asm_tzcnt(hit));
        }
        bit += take;
    }
    return -1;
}

/* =========================================================================
 * 二、页级按需物理挂载与空页回收引擎 (Populate-on-Alloc / Depopulate-on-Free)
 * ========================================================================= */

/* 解除指定逻辑页掩码在所有 CPU 上的页表映射并归还物理页给 Buddy (自动跳过 pinned_map 固定页) */
static void pcpu_depopulate_mask(pcpu_chunk_t *chunk, uint64 page_mask) {
    uint64 unmap_mask = page_mask & chunk->populated_map & ~chunk->pinned_map;
    if (unmap_mask == 0ULL) {
        return;
    }

    uint32 nr_cpus = cpu_topo.nr_possible_cpus;
    while (unmap_mask != 0ULL) {
        uint32 p = (uint32)asm_tzcnt(unmap_mask);
        unmap_mask &= (unmap_mask - 1ULL);

        /* 对全部 nr_possible_cpus 解除第 p 页映射并释放 4KB 物理页
         * 注：若多核运行期 vm_unmap_range 尚未内置跨核 TLB Shootdown，后续需补发 IPI 刷新远程核 TLB
         */
        for (uint32 i = 0; i < nr_cpus; i++) {
            uint64 va = chunk->base_va
                      + ((uint64)i * PCPU_CHUNK_SIZE)
                      + ((uint64)p << PCPU_PAGE_SHIFT);
            vm_unmap_range(&kernel_space, va, PCPU_PAGE_SIZE, UNMAP_FLAG_FREE_PHYS);
        }

        chunk->populated_map &= ~(1ULL << p);
    }
}

/* 确保逻辑页区间 [first_page, last_page] 在所有 CPU 上均已挂载有效 4KB 物理页
 * 特性：
 *   1) 已挂载页 O(1) 位运算直接跳过；
 *   2) 仅申请 order=0 单页，彻底免疫高阶 Buddy 碎片 OOM；
 *   3) 任意核 OOM 时完整回滚本次已映射的全部物理页，保证原子性零泄漏。
 */
static boolean pcpu_populate_range(pcpu_chunk_t *chunk, uint32 first_page, uint32 last_page) {
    uint64 want_mask = pcpu_page_range_mask(first_page, last_page);
    uint64 missing   = want_mask & ~chunk->populated_map;

    /* 快路径：所需逻辑页全部已挂载，直接放行 */
    if (missing == 0ULL) {
        return TRUE;
    }

    uint32 nr_cpus             = cpu_topo.nr_possible_cpus;
    uint64 populated_this_call = 0ULL;

    while (missing != 0ULL) {
        uint32 p = (uint32)asm_tzcnt(missing);
        missing &= (missing - 1ULL);

        for (uint32 i = 0; i < nr_cpus; i++) {
            /* 申请单页 (order=0) 物理内存：
             * 若你的伙伴系统已开放按节点分配接口，可直接替换为：
             *   alloc_pages_node(0, cpu_lid_to_numa_lid(i))
             * 以实现 100% 运行期 NUMA 本地物理页亲和。
             */
            page_t *page = alloc_pages_node(0,cpu_lid_to_numa_lid(i));
            if (!page) {
                PR_WARN("[PERCPU] alloc_pages(order=0) OOM at chunk=%#lx page=%d CPU#%d, rolling back.\n",
                        chunk->base_va, p, i);

                /* 回滚当前页 p 已为 CPU 0..i-1 映射的物理页 */
                for (uint32 j = 0; j < i; j++) {
                    uint64 rollback_va = chunk->base_va
                                       + ((uint64)j * PCPU_CHUNK_SIZE)
                                       + ((uint64)p << PCPU_PAGE_SHIFT);
                    vm_unmap_range(&kernel_space, rollback_va, PCPU_PAGE_SIZE, UNMAP_FLAG_FREE_PHYS);
                }
                /* 回滚本次调用之前已完整挂载的其他页 */
                pcpu_depopulate_mask(chunk, populated_this_call);
                return FALSE;
            }

            uint64 pa = page_to_pa(page);
            uint64 va = chunk->base_va
                      + ((uint64)i * PCPU_CHUNK_SIZE)
                      + ((uint64)p << PCPU_PAGE_SHIFT);
            /* Not-Present -> Present 映射在 x86-64 上无需跨核 TLB Shootdown */
            vm_map_range(&kernel_space, va, pa, PCPU_PAGE_SIZE, PAGE_KERNEL_DATA_RW);
        }

        chunk->populated_map |= (1ULL << p);
        populated_this_call  |= (1ULL << p);
    }

    return TRUE;
}

/* =========================================================================
 * 三、页级与 Chunk 级空闲摘要维护 (游程跳跃扫描，仅重算受影响的 4KB 页)
 * ========================================================================= */

/* 重算单个 4KB 逻辑页 (8 个 uint64 字) 的空闲摘要 */
static void pcpu_refresh_page_md(pcpu_chunk_t *chunk, uint32 page_idx) {
    uint32 base_word   = page_idx * PCPU_WORDS_PER_PAGE;
    pcpu_page_md_t *md = &chunk->page_md[page_idx];

    uint16  first_free = PCPU_BITS_PER_PAGE;
    uint16  max_contig = 0;
    uint16  left_free  = 0;
    uint16  cur_run    = 0;
    boolean leading    = TRUE;

    for (uint32 w = 0; w < PCPU_WORDS_PER_PAGE; w++) {
        uint64 val      = chunk->alloc_map[base_word + w];
        uint16 word_off = (uint16)(w << 6);

        if (val == 0ULL) {
            if (first_free == PCPU_BITS_PER_PAGE) {
                first_free = word_off;
            }
            cur_run += 64U;
            if (leading) {
                left_free += 64U;
            }
            if (cur_run > max_contig) {
                max_contig = cur_run;
            }
            continue;
        }

        if (val == ~0ULL) {
            leading = FALSE;
            cur_run = 0;
            continue;
        }

        /* 混合字：利用硬件 TZCNT 按连续 0/1 游程跳跃统计 */
        uint32 pos = 0;
        while (pos < 64U) {
            uint64 shifted = val >> pos;
            if ((shifted & 1ULL) == 0ULL) {
                uint32 zeros = (shifted == 0ULL) ? (64U - pos) : (uint32)asm_tzcnt(shifted);
                if (first_free == PCPU_BITS_PER_PAGE) {
                    first_free = (uint16)(word_off + pos);
                }
                cur_run += (uint16)zeros;
                if (leading) {
                    left_free += (uint16)zeros;
                }
                if (cur_run > max_contig) {
                    max_contig = cur_run;
                }
                pos += zeros;
            } else {
                leading = FALSE;
                cur_run = 0;
                uint64 inv  = ~shifted;
                uint32 ones = (inv == 0ULL) ? (64U - pos) : (uint32)asm_tzcnt(inv);
                if (pos + ones > 64U) {
                    ones = 64U - pos;
                }
                pos += ones;
            }
        }
    }

    md->first_free  = first_free;
    md->contig_hint = max_contig;
    md->left_free   = left_free;
    md->right_free  = cur_run;
}

/* 聚合 64 页摘要，更新整个 Chunk 的 first_free 与精确最大连续空闲块 (含跨页拼接) */
static void pcpu_refresh_chunk_md(pcpu_chunk_t *chunk) {
    uint32 chunk_first  = PCPU_CHUNK_BITS;
    uint32 max_contig   = 0;
    uint32 cross_contig = 0;

    for (uint32 p = 0; p < PCPU_CHUNK_PAGES; p++) {
        const pcpu_page_md_t *md = &chunk->page_md[p];

        if (chunk_first == PCPU_CHUNK_BITS && md->first_free < PCPU_BITS_PER_PAGE) {
            chunk_first = (p << PCPU_BITS_PER_PAGE_SHIFT) + md->first_free;
        }
        if (md->contig_hint > max_contig) {
            max_contig = md->contig_hint;
        }

        uint32 combined = cross_contig + md->left_free;
        if (combined > max_contig) {
            max_contig = combined;
        }

        if (md->left_free == PCPU_BITS_PER_PAGE) {
            cross_contig += PCPU_BITS_PER_PAGE;
        } else {
            cross_contig = md->right_free;
        }
    }

    chunk->first_free  = chunk_first;
    chunk->contig_hint = max_contig;
}

/* 将一个新建 Chunk 初始化为全空、零物理页挂载状态 */
static void pcpu_chunk_init_empty(pcpu_chunk_t *chunk, uint64 base_va) {
    asm_mem_set(chunk, 0, sizeof(pcpu_chunk_t));
    list_head_init(&chunk->node);

    chunk->base_va       = base_va;
    chunk->free_bits     = PCPU_CHUNK_BITS;
    chunk->first_free    = 0;
    chunk->contig_hint   = PCPU_CHUNK_BITS;
    chunk->populated_map = 0ULL;
    chunk->pinned_map    = 0ULL;

    for (uint32 p = 0; p < PCPU_CHUNK_PAGES; p++) {
        chunk->page_md[p].first_free  = 0;
        chunk->page_md[p].contig_hint = PCPU_BITS_PER_PAGE;
        chunk->page_md[p].left_free   = PCPU_BITS_PER_PAGE;
        chunk->page_md[p].right_free  = PCPU_BITS_PER_PAGE;
    }
}

/* =========================================================================
 * 四、双位图提交与带对齐约束的首次适应 (First-Fit) 搜索核心
 * ========================================================================= */

/* 在双位图中提交 [start_bit, start_bit + nr_bits) 分配并刷新受影响页摘要 */
static void pcpu_chunk_commit_alloc(pcpu_chunk_t *chunk, uint32 start_bit, uint32 nr_bits) {
    uint32 end_bit = start_bit + nr_bits - 1U;

    pcpu_bitmap_set_range(chunk->alloc_map, start_bit, nr_bits);
    pcpu_bitmap_set_bit(chunk->bound_map, end_bit);
    chunk->free_bits -= nr_bits;

    uint32 first_page = start_bit >> PCPU_BITS_PER_PAGE_SHIFT;
    uint32 last_page  = end_bit   >> PCPU_BITS_PER_PAGE_SHIFT;
    for (uint32 p = first_page; p <= last_page; p++) {
        pcpu_refresh_page_md(chunk, p);
    }
    pcpu_refresh_chunk_md(chunk);
}

/* 首次适应搜索：注意对外要求按绝对虚拟地址对齐，因此同时兼容任意 4KB 对齐的 vmalloc base_va */
static int32 pcpu_find_fit_in_chunk(const pcpu_chunk_t *chunk,
                                    uint32 nr_bits,
                                    uint32 align_bits) {
    if (chunk->free_bits < nr_bits || chunk->contig_hint < nr_bits) {
        return -1;
    }

    /* 若 vmalloc 返回的 base_va 对齐粒度小于请求的 align (如 base_va 仅 4KB 对齐但请求 16KB 对齐)，
     * 计算补偿相位 align_phase，保证 (chunk->base_va + pos * 8) 严格满足 align 对齐！
     */
    uint32 align_mask  = align_bits - 1U;
    uint32 base_bits   = (uint32)((chunk->base_va >> PCPU_MIN_ALLOC_SHIFT) & align_mask);
    uint32 align_phase = (align_bits - base_bits) & align_mask;

    uint32 pos = ALIGN_UP_PHASE(chunk->first_free);

    while (pos + nr_bits <= PCPU_CHUNK_BITS) {
        uint32 page_idx = pos >> PCPU_BITS_PER_PAGE_SHIFT;
        uint32 page_off = pos & (PCPU_BITS_PER_PAGE - 1U);
        const pcpu_page_md_t *md = &chunk->page_md[page_idx];

        /* 剪枝 1：整页已满 -> 直跳下一页 */
        if (md->first_free == PCPU_BITS_PER_PAGE) {
            pos = ALIGN_UP_PHASE((page_idx + 1U) << PCPU_BITS_PER_PAGE_SHIFT);
            continue;
        }

        /* 剪枝 2：当前游标落在本页首个空闲位之前 -> 直跳 first_free */
        if (page_off < md->first_free) {
            pos = ALIGN_UP_PHASE((page_idx << PCPU_BITS_PER_PAGE_SHIFT) + md->first_free);
            continue;
        }

        /* 剪枝 3：本页最大连续空洞不足 -> 仅可能从页尾 right_free 发起跨页分配 */
        if (md->contig_hint < nr_bits) {
            uint32 tail_start = (page_idx << PCPU_BITS_PER_PAGE_SHIFT)
                              + (PCPU_BITS_PER_PAGE - md->right_free);
            if (pos < tail_start) {
                pos = ALIGN_UP_PHASE(tail_start);
                continue;
            }
        }

        /* 字长级精确校验 [pos, pos + nr_bits) 是否全为 0 */
        int32 bad_bit = pcpu_bitmap_find_first_set(chunk->alloc_map, pos, nr_bits);
        if (bad_bit < 0) {
            return (int32)pos; /* 首次适应命中 */
        }

        /* 遇到已占用位 -> 直接跳过冲突位并重新按相位对齐 */
        pos = ALIGN_UP_PHASE((uint32)bad_bit + 1U);
    }

    return -1;
}

/* =========================================================================
 * 五、基于 vmalloc 的无限 Chunk 动态扩容与全空 Chunk 回收
 * ========================================================================= */

/* 创建新 Chunk：仅通过 vmalloc 预留虚拟区间 (0 字节物理页消耗)，具体页交由按需挂载 */
static pcpu_chunk_t *pcpu_expand_chunk(void) {
    uint64 total_vsize = (uint64)cpu_topo.nr_possible_cpus * PCPU_CHUNK_SIZE;

    /* 1. 向 vmalloc 申请一片连续的虚拟地址空间 (无限扩容的核心) */
    uint64 base_va = pcpu_vm_reserve_range(total_vsize);
    if (base_va == 0ULL) {
        PR_WARN("[PERCPU] vmalloc virtual range exhausted (need %ld bytes)!\n", total_vsize);
        return NULL;
    }

    /* 2. 分配并初始化 Chunk 控制块 (此时不分配任何物理载荷页) */
    pcpu_chunk_t *new_chunk = kmalloc(sizeof(pcpu_chunk_t));
    if (!new_chunk) {
        pcpu_vm_release_range(base_va, total_vsize);
        return NULL;
    }

    pcpu_chunk_init_empty(new_chunk, base_va);

    /* 挂入链表尾部：优先复用既有 Chunk (尤其是已挂载热页的 Chunk 0)，提升 dTLB 密度 */
    list_add_tail(&new_chunk->node, &chunk_list);
    return new_chunk;
}

/* 彻底回收全空的非创世 Chunk：解除残留物理页、归还 vmalloc 虚拟区间并释放结构体 */
static void pcpu_recycle_empty_chunk(pcpu_chunk_t *chunk) {
    if (chunk == &first_chunk || chunk->free_bits != PCPU_CHUNK_BITS) {
        return;
    }

    /* 防御性确保所有物理页均已解除映射并归还 Buddy */
    if (chunk->populated_map != 0ULL) {
        pcpu_depopulate_mask(chunk, chunk->populated_map);
    }

    uint64 total_vsize = (uint64)cpu_topo.nr_possible_cpus * PCPU_CHUNK_SIZE;
    list_del(&chunk->node);
    pcpu_vm_release_range(chunk->base_va, total_vsize);
    kfree(chunk);
}

/* =========================================================================
 * 六、对外公开接口：创世 Chunk 0 初始化、无限动态分配与按页回收释放
 * ========================================================================= */

/* 由 alloc_percpu_init() 在完成 Chunk 0 早期 memblock 预映射后调用 */
void pcpu_first_chunk_setup(uint64 base_va, uint32 reserved_bytes, uint32 premapped_pages) {
    if (reserved_bytes > PCPU_CHUNK_SIZE || premapped_pages > PCPU_CHUNK_PAGES) {
        PR_ERROR("[PERCPU] Invalid first chunk setup: reserved=%d, premapped_pages=%d (max=%d)\n",
                 reserved_bytes, premapped_pages, PCPU_CHUNK_PAGES);
        while (1) asm_hlt();
    }

    list_head_init(&chunk_list);
    pcpu_alloc_lock = 0;

    pcpu_chunk_init_empty(&first_chunk, base_va);

    /* 记录已由 memblock 预映射的页，ap 并同时置入 pinned_m防止运行期误还给 Buddy */
    if (premapped_pages > 0) {
        uint64 init_mask          = pcpu_page_range_mask(0, premapped_pages - 1U);
        first_chunk.populated_map = init_mask;
        first_chunk.pinned_map    = init_mask;
    }

    /* 在 Chunk 0 偏移 0 处永久锁定 cpu_control_block_t 所占空间 */
    if (reserved_bytes > 0) {
        uint32 nr_bits = (reserved_bytes + PCPU_MIN_ALLOC_SIZE - 1U) >> PCPU_MIN_ALLOC_SHIFT;
        first_chunk_reserved_bits = nr_bits;
        pcpu_chunk_commit_alloc(&first_chunk, 0, nr_bits);
    } else {
        first_chunk_reserved_bits = 0;
    }

    list_add_tail(&first_chunk.node, &chunk_list);
}

/**
 * 动态分配每核私有内存 (双位图 + 首次适应 + vmalloc 无限扩容 + 分配期按页按需挂载)
 * @param size  请求字节数 (1 .. PCPU_CHUNK_SIZE)
 * @param align 对齐字节数 (自动规整为 >= 8 且为 2 的幂，上限 PCPU_CHUNK_SIZE)
 * @return      相对 %gs 偏移量指针 (可直接配合 %gs / this_cpu_ptr / per_cpu_ptr)；失败返回 NULL
 */
void *alloc_percpu(uint32 size, uint32 align) {
    if (size == 0 || size > PCPU_CHUNK_SIZE) {
        PR_WARN("[PERCPU] Invalid alloc size: %d (max %ld)\n", size, PCPU_CHUNK_SIZE);
        return NULL;
    }

    if (align < PCPU_MIN_ALLOC_SIZE) {
        align = PCPU_MIN_ALLOC_SIZE;
    }
    if (align > PCPU_CHUNK_SIZE) {
        PR_WARN("[PERCPU] Invalid align: %d (max %ld)\n", align, PCPU_CHUNK_SIZE);
        return NULL;
    }
    align = pcpu_roundup_pow2(align);

    uint32 nr_bits     = (size + PCPU_MIN_ALLOC_SIZE - 1U) >> PCPU_MIN_ALLOC_SHIFT;
    uint32 align_bits  = align >> PCPU_MIN_ALLOC_SHIFT;
    uint32 alloc_bytes = nr_bits << PCPU_MIN_ALLOC_SHIFT;

    uint64  target_base_va      = 0ULL;
    uint32  target_in_chunk_off = 0U;
    boolean success             = FALSE;

    spin_lock(&pcpu_alloc_lock);

    /* 1. 遍历所有现有 Chunk 执行首次适应搜索 (优先复用 Chunk 0 及已有热页) */
    list_head_t *pos;
    list_for_each(pos, &chunk_list) {
        pcpu_chunk_t *chunk = CONTAINER_OF(pos, pcpu_chunk_t, node);
        int32 bit = pcpu_find_fit_in_chunk(chunk, nr_bits, align_bits);
        if (bit >= 0) {
            uint32 start_bit  = (uint32)bit;
            uint32 end_bit    = start_bit + nr_bits - 1U;
            uint32 first_page = start_bit >> PCPU_BITS_PER_PAGE_SHIFT;
            uint32 last_page  = end_bit   >> PCPU_BITS_PER_PAGE_SHIFT;

            /* 分配期按页按需挂载物理页：确保返回前全核页表 100% 就绪，绝不触发运行期 #PF */
            if (!pcpu_populate_range(chunk, first_page, last_page)) {
                spin_unlock(&pcpu_alloc_lock);
                return NULL;
            }

            pcpu_chunk_commit_alloc(chunk, start_bit, nr_bits);
            target_base_va      = chunk->base_va;
            target_in_chunk_off = start_bit << PCPU_MIN_ALLOC_SHIFT;
            success             = TRUE;
            break;
        }
    }

    /* 2. 现有 Chunk 均无法满足 -> 通过 vmalloc 无限扩容一个新 256KB Chunk */
    if (!success) {
        pcpu_chunk_t *new_chunk = pcpu_expand_chunk();
        if (new_chunk) {
            int32 bit = pcpu_find_fit_in_chunk(new_chunk, nr_bits, align_bits);
            if (bit >= 0) {
                uint32 start_bit  = (uint32)bit;
                uint32 end_bit    = start_bit + nr_bits - 1U;
                uint32 first_page = start_bit >> PCPU_BITS_PER_PAGE_SHIFT;
                uint32 last_page  = end_bit   >> PCPU_BITS_PER_PAGE_SHIFT;

                if (pcpu_populate_range(new_chunk, first_page, last_page)) {
                    pcpu_chunk_commit_alloc(new_chunk, start_bit, nr_bits);
                    target_base_va      = new_chunk->base_va;
                    target_in_chunk_off = start_bit << PCPU_MIN_ALLOC_SHIFT;
                    success             = TRUE;
                } else {
                    /* 物理页挂载 OOM：立即回收刚才新建的空 Chunk，零虚拟空间泄漏 */
                    pcpu_recycle_empty_chunk(new_chunk);
                }
            } else {
                /* 极端情况：vmalloc 地址对齐粒度不足以容纳超高对齐请求 (如 align == 256KB) */
                PR_WARN("[PERCPU] New vmalloc chunk %#lx cannot satisfy align=%d size=%d\n",
                        new_chunk->base_va, align, size);
                pcpu_recycle_empty_chunk(new_chunk);
            }
        }
    }

    spin_unlock(&pcpu_alloc_lock);

    if (!success) {
        return NULL;
    }

    /* 3. 锁外清零所有 CPU 副本的新分配区域，防止复用脏数据并大幅缩短持锁时间 */
    uint32 nr_cpus = cpu_topo.nr_possible_cpus;
    for (uint32 i = 0; i < nr_cpus; i++) {
        void *cpu_dst = (void *)(target_base_va
                               + ((uint64)i * PCPU_CHUNK_SIZE)
                               + target_in_chunk_off);
        asm_mem_set(cpu_dst, 0, alloc_bytes);
    }

    /* 4. 计算相对创世 Chunk 0 的 64 位模差偏移量 (与 %gs 基址自动抵消核间步长) */
    uint64 delta = (target_base_va - first_chunk.base_va) + target_in_chunk_off;
    return (void *)delta;
}

/**
 * 释放动态 Per-CPU 内存 (免传 size，自动隐式合并、空页物理归还与空 Chunk 虚拟回收)
 * @param ptr alloc_percpu 返回的相对偏移量指针
 */
void free_percpu(void *ptr) {
    if (!ptr) {
        return;
    }

    /* 还原为该对象在 CPU 0 视图下的真实虚拟地址 */
    uint64 cpu0_va = first_chunk.base_va + (uint64)ptr;

    spin_lock(&pcpu_alloc_lock);

    /* 1. 定位所属 Chunk (Chunk 数量极少，直接范围匹配兼容任意 vmalloc 对齐地址) */
    pcpu_chunk_t *target_chunk = NULL;
    list_head_t  *pos;
    list_for_each(pos, &chunk_list) {
        pcpu_chunk_t *chunk = CONTAINER_OF(pos, pcpu_chunk_t, node);
        if (cpu0_va >= chunk->base_va && cpu0_va < (chunk->base_va + PCPU_CHUNK_SIZE)) {
            target_chunk = chunk;
            break;
        }
    }

    if (!target_chunk) {
        spin_unlock(&pcpu_alloc_lock);
        PR_WARN("[PERCPU] free_percpu: invalid offset %#lx (cpu0_va=%#lx)!\n",
                (uint64)ptr, cpu0_va);
        return;
    }

    uint32 in_chunk_off = (uint32)(cpu0_va - target_chunk->base_va);
    if ((in_chunk_off & (PCPU_MIN_ALLOC_SIZE - 1U)) != 0U) {
        spin_unlock(&pcpu_alloc_lock);
        PR_WARN("[PERCPU] free_percpu: unaligned offset %#lx!\n", (uint64)ptr);
        return;
    }

    uint32 start_bit = in_chunk_off >> PCPU_MIN_ALLOC_SHIFT;

    /* 2. 保护创世 Chunk 0 头部永久保留的 CCB 区域 */
    if (target_chunk == &first_chunk && start_bit < first_chunk_reserved_bits) {
        spin_unlock(&pcpu_alloc_lock);
        PR_WARN("[PERCPU] free_percpu: attempt to free reserved CCB offset %#lx!\n", (uint64)ptr);
        return;
    }

    /* 3. 校验 Double Free 与块内野指针 */
    if (!pcpu_bitmap_test(target_chunk->alloc_map, start_bit)) {
        spin_unlock(&pcpu_alloc_lock);
        PR_WARN("[PERCPU] free_percpu: double free at offset %#lx!\n", (uint64)ptr);
        return;
    }
    if (start_bit > 0 &&
        pcpu_bitmap_test(target_chunk->alloc_map, start_bit - 1U) &&
        !pcpu_bitmap_test(target_chunk->bound_map, start_bit - 1U)) {
        spin_unlock(&pcpu_alloc_lock);
        PR_WARN("[PERCPU] free_percpu: interior pointer free at offset %#lx!\n", (uint64)ptr);
        return;
    }

    /* 4. 利用 bound_map 定位对象精确末位 end_bit */
    int32 end_bit_res = pcpu_bitmap_find_first_set(target_chunk->bound_map,
                                                   start_bit,
                                                   PCPU_CHUNK_BITS - start_bit);
    if (end_bit_res < 0) {
        spin_unlock(&pcpu_alloc_lock);
        PR_ERROR("[PERCPU] free_percpu: corrupted bound_map at offset %#lx!\n", (uint64)ptr);
        return;
    }

    uint32 end_bit = (uint32)end_bit_res;
    uint32 nr_bits = end_bit - start_bit + 1U;

    /* 5. 清除占用区间与末位标志 (相邻空闲区域自然隐式合并) */
    pcpu_bitmap_clear_range(target_chunk->alloc_map, start_bit, nr_bits);
    pcpu_bitmap_clear_bit(target_chunk->bound_map, end_bit);
    target_chunk->free_bits += nr_bits;

    /* 6. 刷新受影响页摘要，收集已彻底变空的非固定 4KB 页并归还 Buddy */
    uint32 first_page = start_bit >> PCPU_BITS_PER_PAGE_SHIFT;
    uint32 last_page  = end_bit   >> PCPU_BITS_PER_PAGE_SHIFT;
    uint64 empty_mask = 0ULL;

    for (uint32 p = first_page; p <= last_page; p++) {
        pcpu_refresh_page_md(target_chunk, p);
        if (target_chunk->page_md[p].contig_hint == PCPU_BITS_PER_PAGE) {
            empty_mask |= (1ULL << p);
        }
    }
    pcpu_refresh_chunk_md(target_chunk);

    /* 按页粒度及时回收空物理页 (自动跳过 pinned_map 早期页) */
    if (empty_mask != 0ULL) {
        pcpu_depopulate_mask(target_chunk, empty_mask);
    }

    /* 7. 若非创世 Chunk 全部 256KB 已变空，整块归还 vmalloc 虚拟空间 */
    if (target_chunk != &first_chunk && target_chunk->free_bits == PCPU_CHUNK_BITS) {
        pcpu_recycle_empty_chunk(target_chunk);
    }

    spin_unlock(&pcpu_alloc_lock);
}