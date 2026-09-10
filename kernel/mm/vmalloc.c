#include "../include/vmalloc.h"
#include "../include/buddy_system.h"
#include "../include/slub.h"
#include "../include/printk.h"
#include "../init/kernel_page_table.h"

// =========================================================================
// 全局动态虚拟内存布局边界
// =========================================================================
uint64 g_direct_map_start;
uint64 g_direct_map_end;
uint64 g_vmalloc_start;
uint64 g_vmalloc_end;
uint64 g_page_map_start;
uint64 g_page_map_end;
uint64 g_io_map_start;
uint64 g_io_map_end;

/**
 * @brief 初始化全局虚拟内存布局
 * @note  根据 CPU 是否开启 5 级分页 (LA57)，动态划分高半核地址空间。
 *        各个核心区域之间强制插入 Guard Hole (警戒空洞)，彻底阻断跨区越界访问。
 */
INIT_TEXT void vm_layout_init(void) {
    if (tmp_paging_level == 5) {
        // 【5 级页表模式 - LA57】(理论上限 128 PB，单位: PB)
        g_direct_map_start = 0xFF00000000000000ULL;
        g_direct_map_end   = g_direct_map_start + 0x0080000000000000ULL; // 占用 32 PB

        // 【安全隔离】：留出 1 PB 的 Guard Hole！越过直接映射区后不可立即接盘。
        g_vmalloc_start    = g_direct_map_end + 0x0004000000000000ULL;
        g_vmalloc_end      = g_vmalloc_start + 0x0040000000000000ULL;    // vmalloc 占用 16 PB

        g_page_map_start   = g_vmalloc_end + 0x0004000000000000ULL;      // Guard Hole: 1 PB
        g_page_map_end     = g_page_map_start + 0x0004000000000000ULL;   // vmemmap 占用 1 PB

        g_io_map_start     = g_page_map_end + 0x0004000000000000ULL;     // Guard Hole: 1 PB
        g_io_map_end       = g_io_map_start + 0x0004000000000000ULL;     // MMIO 占用 1 PB
    } else {
        // 【4 级页表模式 - LA48】(理论上限 128 TB，单位: TB)
        g_direct_map_start = 0xFFFF800000000000ULL;
        g_direct_map_end   = g_direct_map_start + 0x0000400000000000ULL; // 占用 64 TB

        // 【安全隔离】：留出 1 TB 的 Guard Hole！
        g_vmalloc_start    = g_direct_map_end + 0x0000010000000000ULL;
        g_vmalloc_end      = g_vmalloc_start + 0x0000200000000000ULL;    // vmalloc 占用 32 TB

        g_page_map_start   = g_vmalloc_end + 0x0000010000000000ULL;      // Guard Hole: 1 TB
        g_page_map_end     = g_page_map_start + 0x0000010000000000ULL;   // vmemmap 占用 1 TB

        g_io_map_start     = g_page_map_end + 0x0000010000000000ULL;     // Guard Hole: 1 TB
        g_io_map_end       = g_io_map_start + 0x0000020000000000ULL;     // MMIO 占用 2 TB
    }
}

// =========================================================================
// 核心管理器：忙碌/空闲红黑树双轨制
// =========================================================================

rb_root_t used_vmap_area_root; // 记录已经被分配出去的虚拟内存块 (用于查找释放)
rb_root_t free_vmap_area_root; // 记录目前可用的虚拟内存空闲块 (用于搜索分配)
rb_augment_callbacks_f vmap_area_augment_callbacks; // 增强红黑树的回调操作集

/**
 * @brief 重新计算并维护当前节点及其子树中的最大空闲块容量 (subtree_max_size)
 * @param exit 如果设为 TRUE，且发现计算后的 max 值未发生改变，则触发短路退出(提前终止向上传递)
 */
static boolean compute_max(vmap_area_t *vmap_area, boolean exit) {
    vmap_area_t *child;
    rb_node_t *node = &vmap_area->rb_node;
    uint64 max = vmap_area->va_end - vmap_area->va_start; // 默认自身大小就是最大值

    // 探测左子树，如果有更大的块，更新 max
    if (node->left) {
        child = CONTAINER_OF(node->left, vmap_area_t, rb_node);
        if (child->subtree_max_size > max) max = child->subtree_max_size;
    }
    // 探测右子树，如果有更大的块，更新 max
    if (node->right) {
        child = CONTAINER_OF(node->right, vmap_area_t, rb_node);
        if (child->subtree_max_size > max) max = child->subtree_max_size;
    }

    // 短路优化：如果向上冒泡的过程中发现值没有变化，说明更上层的祖先肯定也不受影响
    if (exit && vmap_area->subtree_max_size == max) return TRUE;

    vmap_area->subtree_max_size = max;
    return FALSE;
}

/**
 * @brief 【回调】在红黑树发生旋转时，修复增强数据
 */
static void vmap_area_augment_rotate(rb_node_t *old_node, rb_node_t *new_node) {
    vmap_area_t *old_vmap = CONTAINER_OF(old_node, vmap_area_t, rb_node);
    vmap_area_t *new_vmap = CONTAINER_OF(new_node, vmap_area_t, rb_node);
    // 旋转后，新父节点继承了老父节点的子树总范围
    new_vmap->subtree_max_size = old_vmap->subtree_max_size;
    // 老节点降级为子节点，需要重新计算它自己的最大值
    compute_max(old_vmap, FALSE);
}

/**
 * @brief 【回调】在红黑树节点被后继者替换时，复制增强数据
 */
static void vmap_area_augment_copy(rb_node_t *old_node, rb_node_t *new_node) {
    vmap_area_t *old_vmap = CONTAINER_OF(old_node, vmap_area_t, rb_node);
    vmap_area_t *new_vmap = CONTAINER_OF(new_node, vmap_area_t, rb_node);
    new_vmap->subtree_max_size = old_vmap->subtree_max_size;
}

/**
 * @brief 【回调】当节点自身大小改变或发生插入删除时，自底向上冒泡修复增强数据
 */
static void vmap_area_augment_propagate(rb_node_t *start_node, rb_node_t *stop_node) {
    while (start_node != stop_node) {
        vmap_area_t *vmap_area = CONTAINER_OF(start_node, vmap_area_t, rb_node);
        // 如果 compute_max 返回 TRUE，触发短路优化，停止向上传播
        if (compute_max(vmap_area, TRUE)) break;
        start_node = rb_parent(start_node);
    }
}

// =========================================================================
// 树节点调度基础操作
// =========================================================================

/**
 * @brief 将一个 VMA 区块插入指定的红黑树中 (按虚拟基址 va_start 排序)
 */
static inline void insert_vmap_area(rb_root_t *root, vmap_area_t *vmap_area, rb_augment_callbacks_f *augment) {
    rb_node_t **link = &root->rb_node;
    rb_node_t *parent = NULL;

    while (*link) {
        parent = *link;
        vmap_area_t *curr = CONTAINER_OF(parent, vmap_area_t, rb_node);
        if (vmap_area->va_start < curr->va_start) {
            link = &parent->left;
        } else if (vmap_area->va_start > curr->va_start) {
            link = &parent->right;
        } else {
            // 理论上不可能发生：虚拟地址区间绝对互斥
            return;
        }
    }
    // 调用底层红黑树接口执行插入，并触发着色平衡
    rb_insert(root, &vmap_area->rb_node, parent, link, augment);
}

/**
 * @brief 将一个 VMA 区块从指定的红黑树中剥离
 */
static inline void erase_vmap_area(rb_root_t *root, vmap_area_t *vmap_area, rb_augment_callbacks_f *augment) {
    rb_erase(root, &vmap_area->rb_node, augment);
}

/**
 * @brief 实例化 VMA 描述符 (修复未初始化悬空指针)
 */
static vmap_area_t *create_vmap_area(uint64 va_start, uint64 va_end, uint64 flags) {
    vmap_area_t *vmap = kmalloc(sizeof(vmap_area_t));
    vmap->va_start = va_start;
    vmap->va_end = va_end;
    vmap->flags = flags;
    vmap->subtree_max_size = 0;

    // 强制赋予初始安全值，杜绝野指针
    vmap->list.next = NULL;
    vmap->list.prev = NULL;
    list_head_init(&vmap->list);

    return vmap;
}

static inline uint64 get_subtree_max_size(rb_node_t *node) {
    if (!node) return 0;
    return (CONTAINER_OF(node, vmap_area_t, rb_node))->subtree_max_size;
}

static inline uint64 get_va_start(rb_node_t *node) {
    if (!node) return 0xFFFFFFFFFFFFFFFFULL;
    return (CONTAINER_OF(node, vmap_area_t, rb_node))->va_start;
}

// =========================================================================
// 核心分配与搜索切分引擎
// =========================================================================

/**
 * @brief 终极修复版：同余寻址算法 (Congruence Alignment)
 * @note  目标是找出一个虚拟地址 VA，使得 (VA % align) == align_offset，且 VA >= addr。
 *        这是支持硬件大页映射（PA 和 VA 必须处于相同 2M/1G 偏移行列）的核心数学支撑。
 */
static inline uint64 get_align_offset_va(uint64 addr, uint64 align, uint64 align_offset) {
    align_offset &= (align - 1);           // 确保传入的期望偏移合法
    uint64 current_offset = addr & (align - 1); // 提取当前地址的对齐偏移

    // 如果当前地址的偏离度还在期望值以内，直接加上差距即可
    if (current_offset <= align_offset) {
        return addr - current_offset + align_offset;
    } else {
        // 如果已经错过了目标偏移，只能跨入下一个对齐周期 (加上一整个 align 步长)
        return addr - current_offset + align + align_offset;
    }
}

/**
 * @brief 最佳适应搜索算法 (修复“大容量诱骗陷阱”)
 */
static inline vmap_area_t *find_vmap_lowest_match(uint64 min_addr, uint64 max_addr,
                                                  uint64 size, uint64 align, uint64 align_offset) {
    rb_node_t *node = free_vmap_area_root.rb_node;
    vmap_area_t *vmap_area, *best_vmap_area = NULL;
    uint64 best_va_start = 0xFFFFFFFFFFFFFFFFUL;

    while (node) {
        vmap_area = CONTAINER_OF(node, vmap_area_t, rb_node);

        uint64 search_start = (vmap_area->va_start > min_addr) ? vmap_area->va_start : min_addr;
        uint64 candidate_va = get_align_offset_va(search_start, align, align_offset);
        uint64 align_va_end = candidate_va + size;

        // 1. 记录最佳候选项
        if (candidate_va >= min_addr && align_va_end <= max_addr && align_va_end <= vmap_area->va_end) {
            if (best_va_start > candidate_va) {
                best_va_start = candidate_va;
                best_vmap_area = vmap_area;
            }
        }

        // 2. 🌟 决定下一步往哪搜 (彻底修复大容量诱惑陷阱)
        // 只有当左子树存在、容量足够，并且【左子树的区间可能落在 min_addr 之后】时，才去左子树！
        // 因为红黑树按 va_start 排序，左子树的所有节点 va_start 必定 < 当前 vmap_area->va_start。
        // 如果当前的 va_start <= min_addr，说明左侧的所有节点必定也 < min_addr，去了也是白去！
        if (vmap_area->va_start > min_addr && get_subtree_max_size(node->left) >= size) {
            node = node->left;
        } else {
            // 左边没戏，且当前已经找到了一个合法解，说明这个解已经是最低地址了，直接斩断搜索！
            if (best_va_start != 0xFFFFFFFFFFFFFFFFUL) {
                break;
            }
            // 否则只能去右子树碰碰运气
            node = node->right;
        }
    }
    return best_vmap_area;
}

/**
 * @brief 区块切割手术刀 (追加底层双向链表的 NULL 安全编织)
 */
static inline vmap_area_t *split_vmap_area(vmap_area_t *vmap_area, uint64 size, uint64 align, uint64 align_offset) {
    uint64 align_va_start = get_align_offset_va(vmap_area->va_start, align, align_offset);
    uint64 align_va_end = align_va_start + size;

    if (align_va_start == vmap_area->va_start && align_va_end == vmap_area->va_end) {
        erase_vmap_area(&free_vmap_area_root, vmap_area, &vmap_area_augment_callbacks);
        return vmap_area;
    }
    else if (align_va_start == vmap_area->va_start) {
        vmap_area_t *new_vmap = create_vmap_area(align_va_start, align_va_end, vmap_area->flags);
        vmap_area->va_start = align_va_end;
        vmap_area_augment_propagate(&vmap_area->rb_node, NULL);

        // 安全编织 (防止 vmap_area 是孤立节点)
        new_vmap->list.prev = vmap_area->list.prev;
        new_vmap->list.next = &vmap_area->list;
        if (vmap_area->list.prev && vmap_area->list.prev != &vmap_area->list) {
            vmap_area->list.prev->next = &new_vmap->list;
        }
        vmap_area->list.prev = &new_vmap->list;
        return new_vmap;
    }
    else if (align_va_end == vmap_area->va_end) {
        vmap_area_t *new_vmap = create_vmap_area(align_va_start, align_va_end, vmap_area->flags);
        vmap_area->va_end = align_va_start;
        vmap_area_augment_propagate(&vmap_area->rb_node, NULL);

        new_vmap->list.next = vmap_area->list.next;
        new_vmap->list.prev = &vmap_area->list;
        if (vmap_area->list.next && vmap_area->list.next != &vmap_area->list) {
            vmap_area->list.next->prev = &new_vmap->list;
        }
        vmap_area->list.next = &new_vmap->list;
        return new_vmap;
    }
    else {
        vmap_area_t *right_free = create_vmap_area(align_va_end, vmap_area->va_end, vmap_area->flags);
        insert_vmap_area(&free_vmap_area_root, right_free, &vmap_area_augment_callbacks);

        vmap_area->va_end = align_va_start;
        vmap_area_augment_propagate(&vmap_area->rb_node, NULL);

        vmap_area_t *new_vmap = create_vmap_area(align_va_start, align_va_end, vmap_area->flags);

        right_free->list.next = vmap_area->list.next;
        right_free->list.prev = &new_vmap->list;
        if (vmap_area->list.next && vmap_area->list.next != &vmap_area->list) {
            vmap_area->list.next->prev = &right_free->list;
        }

        new_vmap->list.next = &right_free->list;
        new_vmap->list.prev = &vmap_area->list;
        vmap_area->list.next = &new_vmap->list;
        return new_vmap;
    }
}

/**
 * @brief 执行底层 VMA 分配，并强制追加 Guard Hole (警戒隔离页)
 */
static vmap_area_t *alloc_vmap_area(uint64 va_start, uint64 va_end, uint64 size, uint64 align, uint64 align_offset, uint64 flags) {
    // 🌟 架构防线：每次分配多要 4KB 的虚无空间作为护城河。
    // 这 4KB 永远不会建立物理映射，一旦驱动发生越界写，会立刻触发缺页异常死机，
    // 而不是静默地污染相邻虚拟区域的数据。
    uint64 real_size = size + PAGE_4K_SIZE;

    vmap_area_t *vmap = find_vmap_lowest_match(va_start, va_end, real_size, align, align_offset);
    if (!vmap) return NULL; // OOM 或没有合适的块

    // 执行精准切割
    vmap = split_vmap_area(vmap, real_size, align, align_offset);
    vmap->flags = flags;

    // 放入忙碌树
    insert_vmap_area(&used_vmap_area_root, vmap, &empty_augment_callbacks);
    return vmap;
}

// =========================================================================
// 回收与碎片整理引擎
// =========================================================================

/**
 * @brief 尝试与前后相邻的空闲区块融为一体 (消除外部碎片)
 */
static inline void merge_free_vmap_area(vmap_area_t *vmap) {
    vmap_area_t *tmp;

    // =========================================================
    // 1. 尝试向左吞噬 (与前面的空闲块合并)
    // =========================================================
    tmp = CONTAINER_OF(vmap->list.prev, vmap_area_t, list);

    // 核心安全防线：
    // a) tmp != vmap: 防止当系统只有一个 VMA 时，自己把自己吞噬
    // b) tmp->flags == 0: 【绝对真理】只有 flags 为 0 的块才是纯正的空闲块！
    // c) 虚拟地址严丝合缝
    if (tmp != vmap && tmp->flags == 0 && vmap->va_start == tmp->va_end) {
        vmap->va_start = tmp->va_start;
        list_del(&tmp->list);
        // 从空闲树中抹除被吞并的左侧节点
        erase_vmap_area(&free_vmap_area_root, tmp, &vmap_area_augment_callbacks);
        kfree(tmp); // 回收 VMA 描述符本身的内存
    }

    // =========================================================
    // 2. 尝试向右吞噬 (与后面的空闲块合并)
    // =========================================================
    tmp = CONTAINER_OF(vmap->list.next, vmap_area_t, list);

    if (tmp != vmap && tmp->flags == 0 && vmap->va_end == tmp->va_start) {
        vmap->va_end = tmp->va_end;
        list_del(&tmp->list);
        // 从空闲树中抹除被吞并的右侧节点
        erase_vmap_area(&free_vmap_area_root, tmp, &vmap_area_augment_callbacks);
        kfree(tmp);
    }
}
/**
 * @brief 将使用完毕的虚拟区间归还大自然
 */
static void free_vmap_area(vmap_area_t *vmap) {
    // 1. 从忙碌树中摘除
    erase_vmap_area(&used_vmap_area_root, vmap, &empty_augment_callbacks);

    // 2. 尝试吞并周遭游散的空闲块
    merge_free_vmap_area(vmap);

    // 3. 洗去一切忙碌属性，还原为纯净的空闲块
    vmap->flags = 0;

    // 4. 重新挂入空闲资源池供下次使用
    insert_vmap_area(&free_vmap_area_root, vmap, &vmap_area_augment_callbacks);
}

/**
 * @brief 通过虚拟地址在忙碌树中反查 VMA 描述符
 */
vmap_area_t *find_vmap_area(uint64 va_start) {
    rb_node_t *node = used_vmap_area_root.rb_node;
    while (node) {
        vmap_area_t *vmap = CONTAINER_OF(node, vmap_area_t, rb_node);
        if (vmap->va_start == va_start) return vmap;

        // 二叉搜索：大了往左走，小了往右走
        node = va_start > vmap->va_start ? node->right : node->left;
    }
    return NULL; // 野指针拦截
}

// =========================================================================
// 顶层 API 层：提供给系统驱动调用的终极接口
// =========================================================================

/**
 * @brief 分配一段逻辑连续、但物理离散的动态内存
 */
void *vmalloc(uint64 size) {
    if (!size) return NULL;
    size = PAGE_4K_ALIGN(size); // 保证基础对齐

    // 从 vmalloc 专属管辖区申请虚拟地址区间
    vmap_area_t *vmap = alloc_vmap_area(g_vmalloc_start, g_vmalloc_end, size, PAGE_4K_SIZE, 0, VM_ALLOC);
    if (!vmap) return NULL;

    uint64 va = vmap->va_start;
    uint64 page_count = size >> PAGE_4K_SHIFT;

    // 物理分配并逐页建立映射
    while (page_count--) {
        page_t *page = alloc_pages(0);
        if (!page) {
            // 🚨 极端情况：分配到一半物理内存枯竭，必须执行严谨的回滚清理！
            vm_unmap_range(&kernel_space, vmap->va_start, va - vmap->va_start, UNMAP_FLAG_FREE_PHYS);
            free_vmap_area(vmap);
            return NULL;
        }
        vm_map_range(&kernel_space, va, page_to_pa(page), PAGE_4K_SIZE, PAGE_KERNEL_DATA_RW);
        va += PAGE_4K_SIZE;
    }
    return (void*)vmap->va_start;
}

/**
 * @brief 释放由 vmalloc 申请的内存
 */
void vfree(void *ptr) {
    if (!ptr) return;
    vmap_area_t *vmap = find_vmap_area((uint64)ptr);
    if (!vmap) return;

    // 🌟 卸载映射与物理内存时，务必排除尾部的那 4KB 警戒页 (Guard Hole)！
    uint64 mapped_size = (vmap->va_end - vmap->va_start) - PAGE_4K_SIZE;

    // 解除虚拟页表树映射，并将真金白银的物理帧归还底层 Buddy System
    vm_unmap_range(&kernel_space, vmap->va_start, mapped_size, UNMAP_FLAG_FREE_PHYS);

    // 摧毁并回收虚拟区间描述符
    free_vmap_area(vmap);
}

/**
 * @brief 贪心大页嗅探雷达
 * @note  根据目标物理区间长度和对齐情况，决断出最适合映射的大页层级(1G/2M/4K)。
 */
static inline uint64 get_optimal_vmap_align(uint64 pa, uint64 size) {
    uint64 pa_end = pa + size;

    // 判断物理区间内是否包含至少一个完整的 1GB 块？
    if (pa_end > PAGE_1G_ALIGN(pa) && (pa_end - PAGE_1G_ALIGN(pa)) >= PAGE_1G_SIZE) return PAGE_1G_SIZE;

    // 判断物理区间内是否包含至少一个完整的 2MB 块？
    if (pa_end > PAGE_2M_ALIGN(pa) && (pa_end - PAGE_2M_ALIGN(pa)) >= PAGE_2M_SIZE) return PAGE_2M_SIZE;

    return PAGE_4K_SIZE; // 兜底使用小页
}

/**
 * @brief 外设寄存器映射引擎底层核心 (支持大页同余着色)
 */
void *_ioremap(uint64 start_pa, uint64 size, uint64 flags) {
    if (size == 0) return NULL;

    // 1. 精准提取页内碎步偏移，确保页表层对齐
    uint64 offset = start_pa & PAGE_4K_OFFSET_MASK;
    uint64 aligned_pa = PAGE_4K_ALIGN_DOWN(start_pa);
    uint64 aligned_size = PAGE_4K_ALIGN(size + offset);

    // 2. 雷达探测：判定最佳映射大页尺寸与所需偏移量
    uint64 optimal_align = get_optimal_vmap_align(aligned_pa, aligned_size);
    uint64 align_offset = aligned_pa & (optimal_align - 1);

    // 3. 在 MMIO 专属管辖区，寻找同余共振的虚拟区间
    vmap_area_t *vmap = alloc_vmap_area(g_io_map_start, g_io_map_end, aligned_size, optimal_align, align_offset, VM_IOREMAP);
    if (!vmap) return NULL;

    // 4. 将指令下达给 VMM 引擎
    // 由于此前的同余铺垫，VMM 下钻时会自动触发贪心升阶，把网卡/显卡等大块寄存器映射为 2M/1G 巨页！
    int32 err = vm_map_range(&kernel_space, vmap->va_start, aligned_pa, aligned_size, flags | SW_FLAG_MAX_1G);
    if (err != 0) {
        free_vmap_area(vmap); // 映射失败，回滚 VMA
        return NULL;
    }

    // 返回带上碎步偏移的精确虚拟指针
    return (void *)(vmap->va_start + offset);
}

/**
 * @brief 将物理外设寄存器映射入虚拟空间 (Uncacheable, 最保守/最安全)
 */
void *ioremap(uint64 start_pa, uint64 size) {
    return _ioremap(start_pa, size, PAGE_KERNEL_MMIO_WUC);
}

/**
 * @brief 将物理外设寄存器映射入虚拟空间 (Write-Combining, 用于显存 Framebuffer 提速)
 */
void *ioremap_wc(uint64 start_pa, uint64 size) {
    return _ioremap(start_pa, size, PAGE_KERNEL_MMIO_WC);
}

/**
 * @brief 解除外设虚拟映射
 */
int32 ioreunmap(void *ptr) {
    if (!ptr) return -1;

    // 抹除尾随偏移，复原 VMA 基址
    uint64 aligned_va = (uint64)ptr & PAGE_4K_MASK;
    vmap_area_t *vmap = find_vmap_area(aligned_va);
    if (!vmap) return -1;

    // 扣除分配时额外附加的 Guard Hole 长度
    uint64 mapped_size = (vmap->va_end - vmap->va_start) - PAGE_4K_SIZE;

    // 卸载页表项 (因外设内存不属于 RAM，无需且严禁释放物理帧，使用 UNMAP_FLAG_NONE)
    vm_unmap_range(&kernel_space, aligned_va, mapped_size, UNMAP_FLAG_NONE);
    free_vmap_area(vmap);
    return 0;
}

/**
 * @brief 将连续的系统保留 RAM (如 ACPI 表) 映射到内核虚址
 */
void *memremap(uint64 start_pa, uint64 size) {
    uint64 aligned_size = PAGE_4K_ALIGN(size);
    vmap_area_t *vmap = alloc_vmap_area(g_vmalloc_start, g_vmalloc_end, aligned_size, PAGE_4K_SIZE, 0, VM_IOREMAP);
    if (!vmap) return NULL;

    // 赋予数据段常用的读写与 Write-Back 缓存属性
    vm_map_range(&kernel_space, vmap->va_start, start_pa, aligned_size, PAGE_KERNEL_DATA_RW);
    return (void*)vmap->va_start;
}

/**
 * @brief 释放 memremap 创建的映射 (逻辑等同于外设释放)
 */
int32 unmemremap(void *ptr) {
    return ioreunmap(ptr);
}

/**
 * @brief 为内核外挂模块 (KO) 提供可执行指令空间
 */
void *module_remap(uint64 start_pa, uint64 size) {
    uint64 aligned_size = PAGE_4K_ALIGN(size);
    // 严格限制在最高位 1.5GB 的 Module 专区
    vmap_area_t *vmap = alloc_vmap_area(MODULES_VA_START, MODULES_VA_END, aligned_size, PAGE_4K_SIZE, 0, VM_MODULES);
    vm_map_range(&kernel_space, vmap->va_start, start_pa, aligned_size, PAGE_KERNEL_DATA_RW);
    return (void*)vmap->va_start;
}

/**
 * @brief 摧毁并回收模块虚址空间 (连同物理指令帧一并拔除)
 */
int32 unmodule_remap(void *ptr) {
    if (!ptr) return -1;
    vmap_area_t *vmap = find_vmap_area((uint64)ptr);
    if (!vmap) return -1;

    uint64 mapped_size = (vmap->va_end - vmap->va_start) - PAGE_4K_SIZE;

    // 模块代码页存放在系统常规内存中，所以必须连带物理帧一起摧毁
    vm_unmap_range(&kernel_space, vmap->va_start, mapped_size, UNMAP_FLAG_FREE_PHYS);
    free_vmap_area(vmap);
    return 0;
}

/**
 * @brief 后门热修饰 API：动态改变已分配区间的硬件属性 (如将代码页设为 NX 只读)
 */
int32 _set_memory_flags(uint64 vaddr, uint64 size, uint64 flags) {
    if (vaddr == 0 || size == 0) return -1;

    // 强制修补可能产生毛刺的入参，确保 VMM 引擎不报错
    uint64 aligned_vaddr = vaddr & PAGE_4K_MASK;
    uint64 aligned_size = PAGE_4K_ALIGN(size + (vaddr - aligned_vaddr));

    vm_protect_range(&kernel_space, aligned_vaddr, aligned_size, flags);
    return 0;
}

// =========================================================================
// 核心模块自举初始化
// =========================================================================
void INIT_TEXT vmalloc_init(void) {
    // 绑定增强红黑树的回调逻辑
    vmap_area_augment_callbacks.rotate = vmap_area_augment_rotate;
    vmap_area_augment_callbacks.copy = vmap_area_augment_copy;
    vmap_area_augment_callbacks.propagate = vmap_area_augment_propagate;

    vmap_area_t *vmap;

    // 创世纪：将三大顶级动态空间作为原始完整的“巨无霸”空闲块，注入资源池

    // 1. 初始化 vmalloc 区 (32TB / 16PB)
    vmap = create_vmap_area(g_vmalloc_start, g_vmalloc_end, 0);
    list_head_init(&vmap->list);
    insert_vmap_area(&free_vmap_area_root, vmap, &vmap_area_augment_callbacks);

    // 2. 初始化 IO 外设映射区 (2TB / 1PB)
    vmap = create_vmap_area(g_io_map_start, g_io_map_end, 0);
    list_head_init(&vmap->list);
    insert_vmap_area(&free_vmap_area_root, vmap, &vmap_area_augment_callbacks);

    // 3. 初始化 Module 代码区 (1.5GB)
    vmap = create_vmap_area(MODULES_VA_START, MODULES_VA_END, 0);
    list_head_init(&vmap->list);
    insert_vmap_area(&free_vmap_area_root, vmap, &vmap_area_augment_callbacks);
}