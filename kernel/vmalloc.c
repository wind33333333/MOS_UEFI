#include "vmalloc.h"
#include "buddy_system.h"
#include "slub.h"
#include "printk.h"
#include "vmm.h"
#include "kpage_table.h"

//忙碌树
rb_root_t used_vmap_area_root;
//空闲树
rb_root_t free_vmap_area_root;

//vmpa_area增强回调函数集
rb_augment_callbacks_f vmap_area_augment_callbacks;

/*
 *计算最大值，当前节点和左右子树取最大值
 */
static BOOLEAN compute_max(vmap_area_t *vmap_area,BOOLEAN exit) {
    vmap_area_t *child;
    rb_node_t *node = &vmap_area->rb_node;
    // 当前节点自身大小
    UINT64 max = vmap_area->va_end - vmap_area->va_start;
    // 比较左子树的取最大值
    if (node->left) {
        child= CONTAINER_OF(node->left,vmap_area_t,rb_node);
        if (child->subtree_max_size > max)
            max = child->subtree_max_size;
    }
    // 比较右子树的取最大值
    if (node->right) {
        child= CONTAINER_OF(node->right,vmap_area_t,rb_node);
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
    vmap_area_t *old_vmap_area=CONTAINER_OF(old_node,vmap_area_t,rb_node);
    vmap_area_t *new_vmap_area=CONTAINER_OF(new_node,vmap_area_t,rb_node);
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
    vmap_area_t *old_vmap_area=CONTAINER_OF(old_node,vmap_area_t,rb_node);
    vmap_area_t *new_vmap_area=CONTAINER_OF(new_node,vmap_area_t,rb_node);
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
        vmap_area_t *vmap_area=CONTAINER_OF(start_node,vmap_area_t,rb_node);
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
static inline UINT32 find_vmap_area_insert_pos(rb_root_t *root, vmap_area_t *vmap_area,rb_node_t **out_parent,rb_node_t **out_link) {
    rb_node_t *parent=NULL,**link = &root->rb_node;
    vmap_area_t *curr_vmap_area;
    //从红黑树找个合适的位置
    while (*link) {
        parent = *link;
        curr_vmap_area = CONTAINER_OF(parent,vmap_area_t,rb_node);
        if (vmap_area->va_start < curr_vmap_area->va_start) {
            link = &parent->left;
        } else if (vmap_area->va_start > curr_vmap_area->va_start) {
            link = &parent->right;
        } else {
            return 1;
        }
    }
    *out_parent = parent;
    *out_link = link;
    return 0;
}

/*
 *把一个vmap_area插入红黑树
 * root：树根
 * vmap_area:需要插入的节点
 * augment_callbacks:红黑树回调增强函数
 */
static inline UINT32 insert_vmap_area(rb_root_t *root, vmap_area_t *vmap_area,rb_augment_callbacks_f *augment_callbacks) {
    rb_node_t *parent,*link;
    find_vmap_area_insert_pos(root,vmap_area,&parent,&link);
    rb_insert(root, &vmap_area->rb_node, parent, link, augment_callbacks);
    return 0;
}

/*
 * 从红黑树删除一个vmap_area
 */
static inline UINT32 erase_vmap_area(rb_root_t *root, vmap_area_t *vmap_area,rb_augment_callbacks_f *augment_callbacks) {
    rb_erase(root, &vmap_area->rb_node,augment_callbacks);
}

//设置空闲状态
static inline set_free(vmap_area_t *vmap_area) {
    vmap_area->flags &=0xFFFFFFFFFFFFFFFEUL;
}

//设置忙碌状态
static inline set_used(vmap_area_t *vmap_area) {
    vmap_area->flags |=1;
}

//判断空闲
static inline BOOLEAN is_free(vmap_area_t *vmap_area) {
    return !(vmap_area->flags&1);
}

//判断忙碌
static inline BOOLEAN is_used(vmap_area_t *vmap_area) {
    return vmap_area->flags&1;
}

//新建一个vmap_area
static vmap_area_t *create_vmap_area(UINT64 va_start,UINT64 va_end,UINT64 flags) {
    vmap_area_t *vmap_area=kmalloc(sizeof(vmap_area_t));
    vmap_area->va_start = va_start;
    vmap_area->va_end   = va_end;
    vmap_area->rb_node.parent_color = 0;
    vmap_area->rb_node.left   = NULL;
    vmap_area->rb_node.right  = NULL;
    vmap_area->list.prev = NULL;
    vmap_area->list.next = NULL;
    vmap_area->subtree_max_size = 0;
    vmap_area->flags = flags;
    return vmap_area;
}

//获取节点subtree_max_size
static inline UINT64 get_subtree_max_size(rb_node_t *node) {
    if (!node)return 0;
    // 通过 rb_entry 获取 vmap_area，返回其 subtree_max_size
    return (CONTAINER_OF(node,vmap_area_t, rb_node))->subtree_max_size;
}

//获取节点va_start
static inline UINT64 get_va_start(rb_node_t *node) {
    if (!node)return 0;
    return (CONTAINER_OF(node,vmap_area_t,rb_node))->va_start;
}

/*低地址优先搜索最佳适应空闲vmap_area*/
static inline vmap_area_t *find_vmap_lowest_match(UINT64 size,UINT64 va_start) {
    rb_node_t *node = free_vmap_area_root.rb_node;
    vmap_area_t *vmap_area;
    while (node) {
        if (get_subtree_max_size(node->left) >= size && get_va_start(node->left) >= va_start) {
            node=node->left;
        }else {
            vmap_area = CONTAINER_OF(node,vmap_area_t,rb_node);
            if ((vmap_area->va_end - vmap_area->va_start) >= size && vmap_area->va_end >= (va_start+size)) return vmap_area;
            node=node->right;
        }
    }
    return NULL;
}

/*
 * 尝试把vmap_area分割到合适大小
 */
static inline vmap_area_t *split_vmap_area(vmap_area_t *vmap_area,UINT64 size,UINT64 va_start) {
    vmap_area_t *new_vmap_area;
    if (size == (vmap_area->va_end-vmap_area->va_start)) {
        //情况1:占用整个
        erase_vmap_area(&free_vmap_area_root,vmap_area,&vmap_area_augment_callbacks);
        new_vmap_area = vmap_area;
    }else if (va_start <= vmap_area->va_start) {
        //情况2：从头切割
        new_vmap_area = create_vmap_area(vmap_area->va_start,vmap_area->va_start+size,vmap_area->flags);
        vmap_area->va_start += size;
        vmap_area_augment_propagate(&vmap_area->rb_node,NULL);
        list_add_tail(&vmap_area->list,&new_vmap_area->list);
    }else if ((va_start+size) == vmap_area->va_end) {
        //情况3：从尾切割
        new_vmap_area = create_vmap_area(va_start,va_start+size,vmap_area->flags);
        vmap_area->va_end -= size;
        vmap_area_augment_propagate(&vmap_area->rb_node,NULL);
        list_add_head(&vmap_area->list,&new_vmap_area->list);
    }else{
        //情况4：从中间切割
        new_vmap_area = create_vmap_area(va_start+size,vmap_area->va_end,vmap_area->flags);
        vmap_area->va_end = va_start;
        vmap_area_augment_propagate(&vmap_area->rb_node,NULL);
        insert_vmap_area(&free_vmap_area_root,new_vmap_area,&vmap_area_augment_callbacks);
        list_add_head(&vmap_area->list,&new_vmap_area->list);
        new_vmap_area = create_vmap_area(va_start,va_start+size,vmap_area->flags);
        list_add_head(&vmap_area->list,&new_vmap_area->list);
    }
    return new_vmap_area;
}

/*
 * 分配一个vmap_area
 * size:需要分配的大小4K对齐
 * va_start:分配的起始地址
 * va_end:结束地址
 */
static vmap_area_t *alloc_vmap_area(UINT64 size,UINT64 va_start,UINT64 va_end) {
    //空闲树找可是的节点
    vmap_area_t *vmap_area=find_vmap_lowest_match(size,va_start);
    if (!vmap_area || vmap_area->va_end > va_end)return NULL;
    //尝试分割Vmap_area
    vmap_area = split_vmap_area(vmap_area,size,va_start);
    //把vmap_area插入忙碌树，设置状态
    set_used(vmap_area);
    insert_vmap_area(&used_vmap_area_root,vmap_area,&empty_augment_callbacks);
    return vmap_area;
}

/*
 * 分配内存
 */
void *vmalloc (UINT64 size) {
    if (!size) return NULL;
    //4k对齐
    size = PAGE_4K_ALIGN(size);
    //分配虚拟地址空间
    vmap_area_t *vmap_area = alloc_vmap_area(size,VMALLOC_START,VMALLOC_END);
    //分配物理页，映射物理页
    UINT64 va = vmap_area->va_start;
    for (UINT64 i=0;i<(size>>PAGE_4K_SHIFT);i++) {
        page_t* page = alloc_pages(0);
        if (!page) return NULL;
        mmap(kpml4t_ptr,page_to_pa(page),(UINT64*)va,PAGE_ROOT_RW_4K,PAGE_4K_SIZE);
        va+=PAGE_4K_SIZE;
    }
    return (void*)vmap_area->va_start;
}

/*
 * 尝试合并左右空闲vmap_area
 */
static inline void merge_free_vmap_area(vmap_area_t *vmap_area) {
    vmap_area_t *tmp_vmap_area;
    //先检查左边是否能合并
    tmp_vmap_area = CONTAINER_OF(vmap_area->list.prev,vmap_area_t,list);
    if (is_free(tmp_vmap_area) && vmap_area->va_start == tmp_vmap_area->va_end) {
        vmap_area->va_start= tmp_vmap_area->va_start;
        list_del(&tmp_vmap_area->list);
        erase_vmap_area(&free_vmap_area_root,tmp_vmap_area,&vmap_area_augment_callbacks);
        kfree(tmp_vmap_area);
    }
    //检查右边是否能合并
    tmp_vmap_area = CONTAINER_OF(vmap_area->list.next,vmap_area_t,list);
    if (is_free(tmp_vmap_area) && vmap_area->va_end == tmp_vmap_area->va_start) {
        vmap_area->va_end = tmp_vmap_area->va_end;
        list_del(&tmp_vmap_area->list);
        erase_vmap_area(&free_vmap_area_root,tmp_vmap_area,&vmap_area_augment_callbacks);
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
    erase_vmap_area(&used_vmap_area_root,vmap_area,&empty_augment_callbacks);
    //尝试合并空闲树相邻vmap_area
    merge_free_vmap_area(vmap_area);
    //插入空闲树,设置转状态空闲
    set_free(vmap_area);
    insert_vmap_area(&free_vmap_area_root,vmap_area,&vmap_area_augment_callbacks);
}

/*
 * 查找vmap_area
 */
vmap_area_t *find_vmap_area(UINT64 va_start) {
    rb_node_t *node=used_vmap_area_root.rb_node;
    while (node) {
        vmap_area_t *vmap_area = CONTAINER_OF(node,vmap_area_t,rb_node);
        if (vmap_area->va_start == va_start) return vmap_area;
        node = va_start > vmap_area->va_start ? node->right : node->left;
    }
    return NULL;
}

/*
 * 释放内存
 */
void vfree (void *ptr) {
    //通过虚拟地址找Vmap_area
    vmap_area_t* vmap_area=find_vmap_area((UINT64)ptr);
    //卸载虚拟地址和物理页映射，释放物理页
    UINT64 va = vmap_area->va_start;
    for (UINT64 i=0;i<(vmap_area->va_end-vmap_area->va_start>>PAGE_4K_SHIFT);i++) {
        page_t* page = pa_to_page(find_page_table_entry(kpml4t_ptr,(void*)va,pte_level)&PAGE_PA_MASK);
        unmmap(kpml4t_ptr,(void*)va,PAGE_4K_SIZE);
        free_pages(page);
        va+=PAGE_4K_SIZE;
    }
    //释放虚拟地址
    free_vmap_area(vmap_area);
}

/*
 * 设备虚拟地址分配和映射
 * pa:物理起始地址
 * attr:属性
 */
void *iomap (UINT64 pa,UINT64 size,UINT64 attr) {
    if (!pa || !size) return NULL;
    //4k对齐
    size = PAGE_4K_ALIGN(size);
    //分配虚拟地址空间
    vmap_area_t *vmap_area = alloc_vmap_area(size,VMALLOC_START,VMALLOC_END);
    //分配物理页，映射物理页
    UINT64 va = vmap_area->va_start;
    for (UINT64 i=0;i<(size>>PAGE_4K_SHIFT);i++) {
        mmap(kpml4t_ptr,pa,(UINT64*)va,attr,PAGE_4K_SIZE);
        va+=PAGE_4K_SIZE;
    }
    return (void*)vmap_area->va_start;
}

/*
 *设备虚拟地址释放和卸载映射
 */
void iounmap (void *ptr) {
    //通过虚拟地址找Vmap_area
    vmap_area_t* vmap_area=find_vmap_area((UINT64)ptr);
    //卸载虚拟地址和物理页映射，释放物理页
    UINT64 va = vmap_area->va_start;
    for (UINT64 i=0;i<(vmap_area->va_end-vmap_area->va_start>>PAGE_4K_SHIFT);i++) {
        unmmap(kpml4t_ptr,(void*)va,PAGE_4K_SIZE);
        va+=PAGE_4K_SIZE;
    }
    //释放虚拟地址
    free_vmap_area(vmap_area);
}

//初始化vmalloc
void INIT_TEXT init_vmalloc(void) {
    vmap_area_augment_callbacks.rotate=vmap_area_augment_rotate;
    vmap_area_augment_callbacks.copy=vmap_area_augment_copy;
    vmap_area_augment_callbacks.propagate=vmap_area_augment_propagate;

    buddy_system.free_count[0] = 0;
    page_t *page=alloc_pages(0);
    free_pages(page);

    //60TB vmalloc映射区
    vmap_area_t *vmap_area=create_vmap_area(VMALLOC_START,VMALLOC_END,VM_ALLOC);
    list_head_init(&vmap_area->list);
    insert_vmap_area(&free_vmap_area_root,vmap_area,&vmap_area_augment_callbacks);

    //3070GB IO/UEFI/ACPI/APIC等映射区
    vmap_area=create_vmap_area(VMIOMAP_START,VMIOMAP_END,VM_IOREMAP);
    list_head_init(&vmap_area->list);
    insert_vmap_area(&free_vmap_area_root,vmap_area,&vmap_area_augment_callbacks);

    //初始化动态模块空间 1536MB
    vmap_area=create_vmap_area(MODULES_START,MODULES_END,VM_MODULES);
    list_head_init(&vmap_area->list);
    insert_vmap_area(&free_vmap_area_root,vmap_area,&vmap_area_augment_callbacks);




};


/* 传入key查找node */
/*rb_node_t *rb_find(rb_root_t *root, UINT64 key) {
    rb_node_t *node = root->rb_node;
    while (node) {
        if (((my_data_t *) node)->key == key) return node; //搜索key对应的节点
        node = key < ((my_data_t *) node)->key ? node->left : node->right;
    }
    return NULL; //没有找到
}*/


/*//中序遍历
void mid_traversal1(rb_root_t *rbtree) {
    rb_node_t *cur_node = rbtree->root;
    while (cur_node->left) {
        //找到最左边的节点
        cur_node = cur_node->left;
    }

    while (cur_node) {
        color_printk(GREEN,BLACK, "key:%d   color:%d\n", cur_node->key, cur_node->color);
        if (cur_node->right) {
            // 有右子树，转向右子树的最左节点
            cur_node = cur_node->right;
            while (cur_node->left) {
                cur_node = cur_node->left;
            }
        } else {
            // 没有右子树，回溯到父节点
            while (cur_node->parent != NULL && cur_node->parent->right == cur_node) {
                cur_node = cur_node->parent;
            }
            cur_node = cur_node->parent;
        }
    }
}*/


//递归中序遍历
/*
void mid_traversal(rb_root_t *root, rb_node_t *node) {
    if (!node) return;
    mid_traversal(root, node->left); //处理左子树
    color_printk(GREEN, BLACK, "key:%d   color:%d\n", ((my_data_t *) node)->key, rb_color(node));
    mid_traversal(root, node->right); //处理右子树
}
*/


