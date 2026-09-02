#include "../include/vmalloc.h"
#include "../include/buddy_system.h"
#include "../include/slub.h"
#include "../include/printk.h"
#include "../init/kernel_page_table.h"

// 默认 4 级页表占位符
uint64 g_direct_map_start;
uint64 g_direct_map_end;
uint64 g_vmalloc_start;
uint64 g_vmalloc_end;
uint64 g_page_map_start;
uint64 g_page_map_end;
uint64 g_io_map_start;
uint64 g_io_map_end;

void vm_layout_init(void) {
    uint64 cr4 = asm_get_cr4();
    if (cr4 & (1<<12)) {
        // =====================================================================
        // 【5 级页表模式 - LA57】(单位: PB = 0x0004000000000000ULL)
        // =====================================================================

        g_direct_map_start = 0xFF00000000000000ULL;
        g_direct_map_end  = g_direct_map_start + 0x0080000000000000ULL; // 占用 32 PB

        // 【安全隔离】：留出 1 PB (0x0004000000000000) 的 Guard Hole,0xFF84000000000000ULL！
        g_vmalloc_start    = g_direct_map_end + 0x0004000000000000ULL;
        g_vmalloc_end     = g_vmalloc_start + 0x0040000000000000ULL;    // 占用 16 PB

        // 【安全隔离】：留出 1 PB 的 Guard Hole,0xFFC8000000000000ULL！
        g_page_map_start   = g_vmalloc_end + 0x0004000000000000ULL;
        g_page_map_end    = g_page_map_start + 0x0004000000000000ULL;   // 占用 1 PB

        // 【安全隔离】：留出 1 PB 的 Guard Hole,0xFFD0000000000000ULL！
        g_io_map_start     = g_page_map_end + 0x0004000000000000ULL;
        g_io_map_end      = g_io_map_start + 0x0004000000000000ULL;     // 占用 1 PB

    } else {
        // =====================================================================
        // 【4 级页表模式 - LA48】(单位: TB = 0x0000010000000000ULL)
        // =====================================================================

        g_direct_map_start = 0xFFFF800000000000ULL;
        g_direct_map_end  = g_direct_map_start + 0x0000400000000000ULL; // 占用 64 TB

        // 【安全隔离】：留出 1 TB (0x0000010000000000) 的 Guard Hole！
        g_vmalloc_start    = g_direct_map_end + 0x0000010000000000ULL;
        g_vmalloc_end     = g_vmalloc_start + 0x0000200000000000ULL;    // 占用 32 TB

        // 【安全隔离】：留出 1 TB 的 Guard Hole！
        g_page_map_start   = g_vmalloc_end + 0x0000010000000000ULL;
        g_page_map_end    = g_page_map_start + 0x0000010000000000ULL;   // 占用 1 TB

        // 【安全隔离】：留出 1 TB 的 Guard Hole！
        g_io_map_start     = g_page_map_end + 0x0000010000000000ULL;
        g_io_map_end      = g_io_map_start + 0x0000020000000000ULL;     // 占用 2 TB
    }
}

//忙碌树
rb_root_t used_vmap_area_root;
//空闲树
rb_root_t free_vmap_area_root;

//vmpa_area增强回调函数集
rb_augment_callbacks_f vmap_area_augment_callbacks;


/*
 *计算最大值，当前节点和左右子树取最大值
 */
static boolean compute_max(vmap_area_t *vmap_area, boolean exit) {
    vmap_area_t *child;
    rb_node_t *node = &vmap_area->rb_node;
    // 当前节点自身大小
    uint64 max = vmap_area->va_end - vmap_area->va_start;
    // 比较左子树的取最大值
    if (node->left) {
        child = CONTAINER_OF(node->left, vmap_area_t, rb_node);
        if (child->subtree_max_size > max)
            max = child->subtree_max_size;
    }
    // 比较右子树的取最大值
    if (node->right) {
        child = CONTAINER_OF(node->right, vmap_area_t, rb_node);
        if (child->subtree_max_size > max)
            max = child->subtree_max_size;
    }
    if (exit && vmap_area->subtree_max_size == max) return TRUE;
    vmap_area->subtree_max_size = max;
    return FALSE;
}

/*
 * 加强旋转
 * old_node:老父节点
 * new_node:新父节点
 */
static void vmap_area_augment_rotate(rb_node_t *old_node, rb_node_t *new_node) {
    vmap_area_t *old_vmap_area = CONTAINER_OF(old_node, vmap_area_t, rb_node);
    vmap_area_t *new_vmap_area = CONTAINER_OF(new_node, vmap_area_t, rb_node);
    //修正新节点的subtree_max_size
    new_vmap_area->subtree_max_size = old_vmap_area->subtree_max_size;
    //修正老节点的subtree_max_size
    compute_max(old_vmap_area,FALSE);
}

/*
 * 加强复制
 * old_node:需要删除的节点
 * new_node:后继节点
 */
static void vmap_area_augment_copy(rb_node_t *old_node, rb_node_t *new_node) {
    vmap_area_t *old_vmap_area = CONTAINER_OF(old_node, vmap_area_t, rb_node);
    vmap_area_t *new_vmap_area = CONTAINER_OF(new_node, vmap_area_t, rb_node);
    //修正后继节点的subtree_max_size
    new_vmap_area->subtree_max_size = old_vmap_area->subtree_max_size;
}

/*
 * 向上修正subtree_max_size
 * start_node:起始节点
 * stop_node:结束节点
 */
static void vmap_area_augment_propagate(rb_node_t *start_node, rb_node_t *stop_node) {
    //向上修正subtree_max_size,当start_node=stop_node推出或者当前节点的subtree_max_size子树subtree_max_size一致时提前退出。
    while (start_node != stop_node) {
        vmap_area_t *vmap_area = CONTAINER_OF(start_node, vmap_area_t, rb_node);
        if (compute_max(vmap_area, TRUE)) break;
        start_node = rb_parent(start_node);
    }
}

/*
 * 二叉查找何时的插入位置
 * root:树根
 * vmap_area:待插入的节点
 * out_parent:返回插入位置节点
 * out_link:返回插入位置的左右子
 */
static inline uint32 find_vmap_area_insert_pos(rb_root_t *root, vmap_area_t *vmap_area, rb_node_t **out_parent,
                                               rb_node_t **out_link) {
    rb_node_t *parent = NULL, **link = &root->rb_node;
    vmap_area_t *curr_vmap_area;
    //从红黑树找个合适的位置
    while (*link) {
        parent = *link;
        curr_vmap_area = CONTAINER_OF(parent, vmap_area_t, rb_node);
        if (vmap_area->va_start < curr_vmap_area->va_start) {
            link = &parent->left;
        } else if (vmap_area->va_start > curr_vmap_area->va_start) {
            link = &parent->right;
        } else {
            return 1;
        }
    }
    *out_parent = parent;
    *out_link = *link;
    return 0;
}

/*
 *把一个vmap_area插入红黑树
 * root：树根
 * vmap_area:需要插入的节点
 * augment_callbacks:红黑树回调增强函数
 */
static inline uint32 insert_vmap_area(rb_root_t *root, vmap_area_t *vmap_area,
                                      rb_augment_callbacks_f *augment_callbacks) {
    rb_node_t *parent, *link;
    find_vmap_area_insert_pos(root, vmap_area, &parent, &link);
    rb_insert(root, &vmap_area->rb_node, parent, &link, augment_callbacks);
    return 0;
}

/*
 * 从红黑树删除一个vmap_area
 */
static inline uint32 erase_vmap_area(rb_root_t *root, vmap_area_t *vmap_area,
                                     rb_augment_callbacks_f *augment_callbacks) {
    rb_erase(root, &vmap_area->rb_node, augment_callbacks);
}

//设置空闲状态
static inline void set_free(vmap_area_t *vmap_area) {
    vmap_area->flags &= 0xFFFFFFFFFFFFFFFEUL;
}

//设置忙碌状态
static inline void set_used(vmap_area_t *vmap_area) {
    vmap_area->flags |= 1;
}

//判断空闲
static inline boolean is_free(vmap_area_t *vmap_area) {
    return !(vmap_area->flags & 1);
}

//判断忙碌
static inline boolean is_used(vmap_area_t *vmap_area) {
    return vmap_area->flags & 1;
}

//新建一个vmap_area
static vmap_area_t *create_vmap_area(uint64 va_start, uint64 va_end, uint64 flags) {
    vmap_area_t *vmap_area = kmalloc(sizeof(vmap_area_t));
    vmap_area->va_start = va_start;
    vmap_area->va_end = va_end;
    vmap_area->rb_node.parent_color = 0;
    vmap_area->rb_node.left = NULL;
    vmap_area->rb_node.right = NULL;
    vmap_area->list.prev = NULL;
    vmap_area->list.next = NULL;
    vmap_area->subtree_max_size = 0;
    vmap_area->flags = flags;
    return vmap_area;
}

//获取节点subtree_max_size
static inline uint64 get_subtree_max_size(rb_node_t *node) {
    if (!node)return 0;
    // 通过 rb_entry 获取 vmap_area，返回其 subtree_max_size
    return (CONTAINER_OF(node, vmap_area_t, rb_node))->subtree_max_size;
}

//获取节点va_start
static inline uint64 get_va_start(rb_node_t *node) {
    if (!node)return 0;
    return (CONTAINER_OF(node, vmap_area_t, rb_node))->va_start;
}

/*低地址优先搜索最佳适应空闲vmap_area*/
static inline vmap_area_t *find_vmap_lowest_match(uint64 min_addr, uint64 max_addr, uint64 size, uint64 align) {
    rb_node_t *node = free_vmap_area_root.rb_node;
    vmap_area_t *vmap_area, *best_vmap_area = NULL;
    uint64 align_va_end, best_va_start = 0xFFFFFFFFFFFFFFFFUL;
    while (node) {
        vmap_area = CONTAINER_OF(node, vmap_area_t, rb_node);
        align_va_end = align_up(vmap_area->va_start, align) + size;
        /* 1. 判断当前区间是否满足：对齐＋大小＋边界 */
        if (align_va_end <= vmap_area->va_end &&\
            vmap_area->va_start >= min_addr &&\
            vmap_area->va_end < max_addr) {
            /* 找到一个可行解，且比之前解的起始更小，则更新最佳解 */
            if (best_va_start > vmap_area->va_start) {
                best_va_start = vmap_area->va_start;
                best_vmap_area = vmap_area; //保存当前适配的vmap_area
            }
        }
        /* 2. 根据左子树的最大容量和左子树起始地址，决定是否进入左子树 */
        if (get_subtree_max_size(node->left) >= size && get_va_start(node->left) >= min_addr) {
            node = node->left; //往左找
            /* 3. 如果当前节点区间已经超出了 max_addr或best_va_start，右子树更大则无需搜索 */
        } else if (vmap_area->va_start > max_addr || vmap_area->va_start >= best_va_start) {
            break;
            /* 4. 否则尝试右子树 */
        } else {
            node = node->right;
        }
    }
    return best_vmap_area;
}

/*
 * 尝试把vmap_area分割到合适大小
 */
static inline vmap_area_t *split_vmap_area(vmap_area_t *vmap_area, uint64 size, uint64 align) {
    vmap_area_t *new_vmap_area;
    uint64 align_va_start = align_up(vmap_area->va_start, align);
    if (vmap_area->va_end - vmap_area->va_start == size) {
        //情况1:占用整个
        erase_vmap_area(&free_vmap_area_root, vmap_area, &vmap_area_augment_callbacks);
        new_vmap_area = vmap_area;
    } else if (align_va_start == vmap_area->va_start) {
        //情况2：从头切割
        new_vmap_area = create_vmap_area(align_va_start, align_va_start + size, vmap_area->flags);
        vmap_area->va_start += size;
        vmap_area_augment_propagate(&vmap_area->rb_node,NULL);
        list_add_tail(&vmap_area->list, &new_vmap_area->list);
    } else if (align_va_start + size == vmap_area->va_end) {
        //情况3：从尾切割
        new_vmap_area = create_vmap_area(align_va_start, align_va_start + size, vmap_area->flags);
        vmap_area->va_end -= size;
        vmap_area_augment_propagate(&vmap_area->rb_node,NULL);
        list_add_head(&vmap_area->list, &new_vmap_area->list);
    } else {
        //情况4：从中间切割
        new_vmap_area = create_vmap_area(align_va_start + size, vmap_area->va_end, vmap_area->flags);
        vmap_area->va_end = align_va_start;
        vmap_area_augment_propagate(&vmap_area->rb_node,NULL);
        insert_vmap_area(&free_vmap_area_root, new_vmap_area, &vmap_area_augment_callbacks);
        list_add_head(&vmap_area->list, &new_vmap_area->list);
        new_vmap_area = create_vmap_area(align_va_start, align_va_start + size, vmap_area->flags);
        list_add_head(&vmap_area->list, &new_vmap_area->list);
    }
    return new_vmap_area;
}

/*
 * 分配一个vmap_area
 * size:需要分配的大小4K对齐
 * va_start:分配的起始地址
 * va_end:结束地址
 */
static vmap_area_t *alloc_vmap_area(uint64 va_start, uint64 va_end, uint64 size, uint64 align) {
    //空闲树找可用的节点
    vmap_area_t *vmap_area = find_vmap_lowest_match(va_start, va_end, size, align);
    if (!vmap_area)return NULL;
    //尝试分割Vmap_area
    vmap_area = split_vmap_area(vmap_area, size, align);
    //把vmap_area插入忙碌树，设置状态
    set_used(vmap_area);
    insert_vmap_area(&used_vmap_area_root, vmap_area, &empty_augment_callbacks);
    return vmap_area;
}

/*
 * 尝试合并左右空闲vmap_area
 */
static inline void merge_free_vmap_area(vmap_area_t *vmap_area) {
    vmap_area_t *tmp_vmap_area;
    //先检查左边是否能合并
    tmp_vmap_area = CONTAINER_OF(vmap_area->list.prev, vmap_area_t, list);
    if (is_free(tmp_vmap_area) && vmap_area->va_start == tmp_vmap_area->va_end) {
        vmap_area->va_start = tmp_vmap_area->va_start;
        list_del(&tmp_vmap_area->list);
        erase_vmap_area(&free_vmap_area_root, tmp_vmap_area, &vmap_area_augment_callbacks);
        kfree(tmp_vmap_area);
    }
    //检查右边是否能合并
    tmp_vmap_area = CONTAINER_OF(vmap_area->list.next, vmap_area_t, list);
    if (is_free(tmp_vmap_area) && vmap_area->va_end == tmp_vmap_area->va_start) {
        vmap_area->va_end = tmp_vmap_area->va_end;
        list_del(&tmp_vmap_area->list);
        erase_vmap_area(&free_vmap_area_root, tmp_vmap_area, &vmap_area_augment_callbacks);
        kfree(tmp_vmap_area);
    }
}

/*释放一个vmap_area
 * 把vmap_area从used_vmap_area_root树
 * 移动到free_vmap_area_root树
 * 检查前后虚拟地址空闲则合并
 */
static void free_vmap_area(vmap_area_t *vmap_area) {
    //从忙碌树释放vmpa_area
    erase_vmap_area(&used_vmap_area_root, vmap_area, &empty_augment_callbacks);
    //尝试合并空闲树相邻vmap_area
    merge_free_vmap_area(vmap_area);
    //插入空闲树,设置转状态空闲
    set_free(vmap_area);
    insert_vmap_area(&free_vmap_area_root, vmap_area, &vmap_area_augment_callbacks);
}

/*
 * 查找vmap_area
 */
vmap_area_t *find_vmap_area(uint64 va_start) {
    rb_node_t *node = used_vmap_area_root.rb_node;
    while (node) {
        vmap_area_t *vmap_area = CONTAINER_OF(node, vmap_area_t, rb_node);
        if (vmap_area->va_start == va_start) return vmap_area;
        node = va_start > vmap_area->va_start ? node->right : node->left;
    }
    return NULL;
}

/*
 * 分配内存
 */
void *vmalloc(uint64 size) {
    if (!size) return NULL;
    //4k对齐
    size = PAGE_4K_ALIGN(size);
    //分配虚拟地址空间
    vmap_area_t *vmap_area = alloc_vmap_area( g_vmalloc_start,g_vmalloc_end, size,PAGE_4K_SIZE);
    //分配物理页，映射物理页
    uint64 va = vmap_area->va_start;
    uint64 page_count = size >> PAGE_4K_SHIFT;
    while (page_count--) {
        page_t *page = alloc_pages(0);
        if (!page) return NULL;
        vm_map_range(&kernel_space,va,page_to_pa(page),PAGE_4K_SIZE,PAGE_KERNEL_DATA_RW);
        va += PAGE_4K_SIZE;
    }
    return (void*)vmap_area->va_start;
}

/*
 * 释放内存
 */
void vfree(void *ptr) {
    //通过虚拟地址找Vmap_area
    vmap_area_t *vmap_area = find_vmap_area((uint64) ptr);
    //卸载虚拟地址和物理页映射，释放物理页
    uint64 va = vmap_area->va_start;
    uint64 page_count = vmap_area->va_end - vmap_area->va_start >> PAGE_4K_SHIFT;
    while (page_count--) {
        vm_unmap_range(&kernel_space,va,PAGE_4K_SIZE);
        va += PAGE_4K_SIZE;
    }
    //释放虚拟地址
    free_vmap_area(vmap_area);
}

/*
 * 设备虚拟地址分配和映射
 * pa:物理起始地址
 * attr:属性
 */
void *_ioremap(uint64 start_pa, uint64 size, uint64 flags) {
    if (size == 0) return NULL;

    uint64 offset = start_pa & PAGE_4K_OFFSET_MASK;
    // 物理地址向下对齐到 4KB
    uint64 aligned_pa = PAGE_4K_ALIGN_DOWN(start_pa);
    // 映射的总长度必须包含偏移量，并向上对齐到 4KB (例如 128字节 + 0x48偏移 -> 需 1 页)
    uint64 aligned_size = PAGE_4K_ALIGN(size);

    //分配虚拟地址空间
    vmap_area_t *vmap_area = alloc_vmap_area(g_io_map_start,g_io_map_end, aligned_size, PAGE_1G_SIZE);
    if (!vmap_area) {
        return NULL; // 🛡️ 防御：虚拟空间耗尽，安全退出
    }

    //映射物理内存
    int32 err = vm_map_range(&kernel_space,vmap_area->va_start,aligned_pa,aligned_size,flags | SW_FLAG_MAX_1G);

    // 4. 🛡️ 错误回滚：如果铺页表时物理内存耗尽，必须释放刚刚申请的 vmap_area！
    if (err != 0) {
        free_vmap_area(vmap_area);
        return NULL;
    }

    return (void *)(vmap_area->va_start + offset);
}


/*
 * 设备虚拟地址释放和卸载映射
 */
int32 ioreunmap(void *ptr) {
    if (!ptr) return -1;

    // 1. 消除传入指针的页内偏移，还原出当初分配的真实 va_start
    uint64 aligned_va = (uint64)ptr & PAGE_4K_MASK;

    // 2. 通过对齐后的虚拟地址找 vmap_area
    vmap_area_t *vmap_area = find_vmap_area(aligned_va);
    if (!vmap_area) {
        return -1; // 🛡️ 防御：野指针或已被释放
    }

    uint64 size = vmap_area->va_end - vmap_area->va_start;

    // 3. 呼叫底层的极速卸载引擎 (自动侦测 1G/2M/4K，级联释放死锁防护)
    vm_unmap_range(&kernel_space,aligned_va, size);

    // 4. 将虚拟地址区间交还给大管家
    free_vmap_area(vmap_area);

    return 0;
}

//用于普通 RAM，或 ACPI 表所在的物理内存。
void *memremap(uint64 start_pa,uint64 size) {
    /*// 1. 如果在常规直接映射区内，极速返回 (O(1))
    if (start_pa <= g_direct_map_start && (start_pa+size) >= g_direct_map_end) {}
    if (is_in_direct_mapping_zone(pa)) {
        return (void *)(pa + PAGE_OFFSET);
    }

    // 2. 🛡️ 架构师安全防线：拦截真正的 IO 寄存器！
    // 检查这段物理地址是不是已经被其他外设声明为 STRICT_IO (严格外设寄存器)
    if (is_strict_mmio_device_address(pa)) {
        // 🚨 致命错误：驱动开发者误用了 memremap 来映射设备寄存器！
        // 如果这里强行映射为 WB 会导致硬件死机，直接拒绝并打印内核警告！
        WARN("FATAL: Try to memremap a strict MMIO device address with WB cache!\n");
        return NULL;
    }*/

    vmap_area_t *vmap_area = alloc_vmap_area( g_vmalloc_start,g_vmalloc_end, size,PAGE_4K_SIZE);
    vm_map_range(&kernel_space,vmap_area->va_start,start_pa,size,PAGE_KERNEL_DATA_RW);
    return (void*)vmap_area->va_start;
}

/*
 * 卸载/释放 memremap 映射的虚拟内存
 * 完美匹配 memremap 的双轨制分配逻辑 (直接映射区 O(1) vs 动态回退)
 */
int32 unmemremap(void *ptr) {
    if (!ptr) return -1;

    uint64 va = (uint64)ptr;

    /*
    // 1. 🛡️ 智能拦截：检查该虚拟地址是否属于线性直接映射区
    if (is_in_direct_mapping_zone(va)) {
        // 🎯 极速路径 (O(1))：
        // 说明当初 memremap 发现这是合法的 RAM，直接返回了 pa + PAGE_OFFSET。
        // 因为直接映射区的页表是开机焊死、全局永久存在的，所以我们【什么都不需要做】！
        // 直接返回成功即可，这极大地节省了 CPU 周期。
        return 0;
    }
    */

    // 2. ↩️ 回退路径 (Fallback)：
    // 如果走到这里，说明这个地址不属于直接映射区（比如它处于 vmalloc 区域）。
    // 这意味着当初 memremap 遇到的是 ACPI 等保留内存，从而回退调用了 __ioremap。
    // 因此，我们只需直接调用 unioremap，让它去查找 vmap_area 并执行级联卸载。
    vmap_area_t *vmap_area = find_vmap_area((uint64) ptr);
    vm_unmap_range(&kernel_space,vmap_area->va_start,vmap_area->va_end-vmap_area->va_start);
    //释放虚拟地址
    free_vmap_area(vmap_area);
}


/*
 * 动态映射可执行的内核代码/驱动模块
 * 强制映射到 MODULES 专区，强制赋予只读+可执行权限
 */
void *module_remap(uint64 start_pa, uint64 size) {
    vmap_area_t *vmap_area = alloc_vmap_area( MODULES_VA_START,MODULES_VA_END , size,PAGE_4K_SIZE);
    vm_map_range(&kernel_space,vmap_area->va_start,start_pa,size,PAGE_KERNEL_DATA_RW);
    return (void*)vmap_area->va_start;
}

/* 卸载可执行代码映射 */
int32 unmodule_remap(void *ptr) {
    //通过虚拟地址找Vmap_area
    vmap_area_t *vmap_area = find_vmap_area((uint64) ptr);
    //卸载虚拟地址和物理页映射，释放物理页
    vm_unmap_range(&kernel_space,vmap_area->va_start,vmap_area->va_end-vmap_area->va_start);
    //释放虚拟地址
    free_vmap_area(vmap_area);
    return 0;
}


int32 _set_memory_flags(uint64 vaddr,uint64 size,uint64 flags) {
    // 强制 4K 对齐处理 (增加代码健壮性，防止传入非对齐的 size 导致拦截)
    uint64 aligned_vaddr = vaddr & PAGE_4K_MASK;
    uint64 aligned_size = PAGE_4K_ALIGN(size + (vaddr - aligned_vaddr));
    vm_protect_range(&kernel_space,aligned_vaddr, aligned_size, flags);
    return 0;
}





//初始化vmalloc
void INIT_TEXT init_vmalloc(void) {
    vmap_area_augment_callbacks.rotate = vmap_area_augment_rotate;
    vmap_area_augment_callbacks.copy = vmap_area_augment_copy;
    vmap_area_augment_callbacks.propagate = vmap_area_augment_propagate;

    //vmalloc映射区
    vmap_area_t *vmap_area = create_vmap_area(g_vmalloc_start,g_vmalloc_end,VM_ALLOC);
    list_head_init(&vmap_area->list);
    insert_vmap_area(&free_vmap_area_root, vmap_area, &vmap_area_augment_callbacks);

    //IO/UEFI/ACPI/APIC等映射区
    vmap_area = create_vmap_area(g_io_map_start,g_io_map_end,VM_IOREMAP);
    list_head_init(&vmap_area->list);
    insert_vmap_area(&free_vmap_area_root, vmap_area, &vmap_area_augment_callbacks);

    //初始化动态模块空间 1536MB
    vmap_area = create_vmap_area(MODULES_VA_START,MODULES_VA_END,VM_MODULES);
    list_head_init(&vmap_area->list);
    insert_vmap_area(&free_vmap_area_root, vmap_area, &vmap_area_augment_callbacks);

};
