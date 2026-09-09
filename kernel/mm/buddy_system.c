#include "../include/buddy_system.h"
#include "../init/kernel_page_table.h"
#include "../include/memblock.h"
#include "../include/printk.h"

buddy_system_t buddy_system;

// =========================================================================
// 伙伴系统初始化 (极致降维 O(1) 加速版)
// =========================================================================
INIT_TEXT void buddy_system_init(void) {
    // 1. 定位 page_map 的虚拟基址
    buddy_system.page_table = (page_t *)g_page_map_start;

    // 2. 初始化各阶空闲链表
    for (uint64 i = 0; i <= MAX_ORDER; i++) {
        list_head_init(&buddy_system.free_area[i].list);
        buddy_system.free_area[i].count = 0;
    }

    // 3. 将 memblock 的连续物理大块，以最大对齐阶数高效切片，喂给伙伴系统
    for (uint32 i = 0; i < memblock.free.count; i++) {
        uint64 start_pa = memblock.free.region[i].start_pa;
        uint64 count    = memblock.free.region[i].size >> 12;

        uint64 start_idx = pa_to_page(start_pa) - buddy_system.page_table;
        uint64 end_idx   = start_idx + count;

        while (start_idx < end_idx) {
            uint32 order = MAX_ORDER;

            // 智能嗅探：寻找当前游标能满足的、且不越界的最高对齐阶数！
            while (order > 0) {
                if ((start_idx & ((1ULL << order) - 1)) == 0 && (start_idx + (1ULL << order) <= end_idx)) {
                    break;
                }
                order--;
            }

            // 获取该超级块的首个 page_t
            page_t *p = buddy_system.page_table + start_idx;

            // 彻底清洗该大块的数据，并将它假装成一个刚刚回收的块扔给 free_pages
            p->order = order;
            p->flags = 0;
            p->refcount = 0;

            free_pages(p); // 这里的释放会极为迅速地挂载到对应阶的 free_area 中

            start_idx += (1ULL << order);
        }
    }

    // 4. 将伙伴系统正式绑定到虚拟内存 VMM 回调接口，接管天下
    kernel_space.ops.alloc_pages = alloc_pages;
    kernel_space.ops.free_pages  = free_pages;
    kernel_space.ops.page_to_phys = page_to_pa;
    kernel_space.ops.phys_to_page = pa_to_page;
    kernel_space.ops.phys_to_virt = pa_to_va;
    kernel_space.ops.virt_to_phys = va_to_pa;
}

// =========================================================================
// 分配器：核心分裂 (Split) 引擎
// =========================================================================
page_t *alloc_pages(uint32 order) {
    if (order > MAX_ORDER) return NULL; // 防止越界死机

    page_t *page = NULL;
    uint32 current_order = order;

    // 1. 向上攀爬：寻找最小可用且非空的高阶块
    while (current_order <= MAX_ORDER) {
        if (buddy_system.free_area[current_order].count > 0) {
            // 从该阶链表中摘除头节点
            page = CONTAINER_OF(buddy_system.free_area[current_order].list.next, page_t, list);
            list_del(&page->list);
            buddy_system.free_area[current_order].count--;
            page->flags &= ~(1ULL << PG_BUDDY); // 摘除闲置标签
            break;
        }
        current_order++;
    }

    if (!page) return NULL; // 内存彻底枯竭 (OOM)

    // 2. 向下劈裂 (Split)：如果拿到的是 4 阶，但只要 2 阶，必须把它对半劈开
    while (current_order > order) {
        current_order--;

        // 🌟 修复 Bug 2：精确计算切出来的另一半 (伙伴块)
        page_t *buddy_half = page + (1ULL << current_order);

        buddy_half->order = current_order;
        buddy_half->flags |= (1ULL << PG_BUDDY); // 必须给伙伴打上空闲印记，否则以后无法合并！

        list_add_head(&buddy_system.free_area[current_order].list, &buddy_half->list);
        buddy_system.free_area[current_order].count++;
    }

    // 3. 构建复合页 (Compound Page) 的头尾关联机制
    page->order = order;
    page->flags = 0; // 洗净标志位，准备交予用户

    if (order > 0) {
        page->flags |= (1ULL << PG_HEAD); // 标记此为复合巨页的头
        for (uint32 i = 1; i < (1ULL << order); i++) {
            // 将所有尾随的子页都打上烙印，并用末位 1 标志这是个尾部指针
            page[i].compound_head = (uint64)page | 1;
            page[i].flags = 0;
        }
    }

    return page;
}

// =========================================================================
// 回收器：核心融合 (Merge) 引擎
// =========================================================================
void free_pages(page_t *page) {
    // 防御性编程：拒收空指针、依然有引用计数的活页，以及复合页的“尾巴”节点
    if (page == NULL || page->refcount > 0) return;
    if (page->compound_head & 1) return; // 绝对禁止单独释放巨页的尾巴！

    uint32 order = page->order;
    page->flags &= ~(1ULL << PG_HEAD); // 撕掉巨页首领标签

    // 1. 尝试与周边的伙伴“同化”并向上攀爬融合
    while (order < MAX_ORDER) {
        // 🌟 修复 Bug 1：必须严格用括号包裹索引减法，再执行按位异或
        uint64 page_idx  = page - buddy_system.page_table;
        uint64 buddy_idx = page_idx ^ (1ULL << order);
        page_t *buddy_page = buddy_system.page_table + buddy_idx;

        // 若伙伴页不在空闲链表中，或者它的阶数跟我不匹配，直接终止融合！
        if (!(buddy_page->flags & (1ULL << PG_BUDDY)) || buddy_page->order != order) {
            break;
        }

        // 伙伴是自由且同阶的！从空闲区中把它硬拽出来合并
        list_del(&buddy_page->list);
        buddy_system.free_area[order].count--;
        buddy_page->flags &= ~(1ULL << PG_BUDDY); // 摘掉伙伴的空闲标记

        // 合并后的超级块的起始指针，永远是地址较小的那个
        if (buddy_idx < page_idx) {
            page = buddy_page;
        }
        order++;
    }

    // 2. 融合终了，将最终的超级块打上印记，挂入对应阶的圣殿
    page->order = order;
    page->flags |= (1ULL << PG_BUDDY); // 🌟 修复 Bug 3：使用绝对安全的标准 C 位或操作
    list_add_head(&buddy_system.free_area[order].list, &page->list);
    buddy_system.free_area[order].count++;
}