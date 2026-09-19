#include "vmalloc.h"
#include "vmm.h"
#include "printk.h"
#include "rbtree.h"

extern rb_root_t used_vmap_area_root; // 记录已经被分配出去的虚拟内存块 (用于查找释放)
extern rb_root_t free_vmap_area_root; // 记录目前可用的虚拟内存空闲块 (用于搜索分配)

vmap_area_t *create_vmap_area(uint64 va_start, uint64 va_end, uint64 flags);
void insert_vmap_area(rb_root_t *root, vmap_area_t *vmap_area, rb_augment_callbacks_f *augment);

extern rb_augment_callbacks_f vmap_area_augment_callbacks;


// =========================================================================
// 核心模块自举初始化
// =========================================================================
void vmalloc_init(void) {
    vmap_area_t *vmap;

    // 创世纪：将三大顶级动态空间作为原始完整的“巨无霸”空闲块，注入资源池

    // 1. 初始化 vmalloc 区 (32TB / 16PB)
    vmap = create_vmap_area(vm_layout.vmalloc_start, vm_layout.vmalloc_end, 0);
    list_head_init(&vmap->list);
    insert_vmap_area(&free_vmap_area_root, vmap, &vmap_area_augment_callbacks);

    // 2. 初始化 IO 外设映射区 (2TB / 1PB)
    vmap = create_vmap_area(vm_layout.io_map_start, vm_layout.io_map_end, 0);
    list_head_init(&vmap->list);
    insert_vmap_area(&free_vmap_area_root, vmap, &vmap_area_augment_callbacks);

    // 3. 初始化 Module 代码区 (1.5GB)
    vmap = create_vmap_area(vm_layout.module_start, vm_layout.module_end, 0);
    list_head_init(&vmap->list);
    insert_vmap_area(&free_vmap_area_root, vmap, &vmap_area_augment_callbacks);

    PR_OK("Vmalloc Memory System Success.\n");
}
