#include "vmalloc.h"

// =========================================================================
// 核心模块自举初始化
// =========================================================================
void vmalloc_init(void) {
    // 绑定增强红黑树的回调逻辑
    vmap_area_augment_callbacks.rotate = vmap_area_augment_rotate;
    vmap_area_augment_callbacks.copy = vmap_area_augment_copy;
    vmap_area_augment_callbacks.propagate = vmap_area_augment_propagate;

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
