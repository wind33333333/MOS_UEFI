#include "vmalloc.h"
#include "slub.h"
#include "printk.h"
#include "vmm.h"

//忙碌树
rb_root_t used_vmap_area_root;
//空闲树
rb_root_t free_vmap_area_root;
//全局虚拟地址链表
list_head_t *vmap_area_list;

//vmpa_area增强回调函数集
rb_augment_callbacks_f vmap_area_augment_callbacks;

/*
 *把一个vmap_area插入红黑树
 * root：树根
 * vmap_area:需要插入的节点
 * augment_callbacks:红黑树回调增强函数
 */
static UINT32 insert_vmap_area(rb_root_t *root, vmap_area_t *vmap_area,rb_augment_callbacks_f *augment_callbacks) {
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
    //把vmap_area连接到红黑树
    rb_link_node(&vmap_area->rb_node, parent, link);
    //修复红黑树插入平衡
    rb_insert_fixup(root, &vmap_area->rb_node,augment_callbacks);
    return 0;
}

/*释放一个vmap_area
 * 把vmap_area从used_vmap_area_root树
 * 移动到free_vmap_area_root树
 * 检查前后虚拟地址空闲则合并
 */
static void free_vmap_area(vmap_area_t *vmap_area) {

}

void *vmalloc (UINT64 size) {
    if (!size) return NULL;
    //4k对齐
    size = PAGE_4K_ALIGN(size);


}


/*
//删除一个vmap_area
static void del_vmap_area(rb_root_t *root, vmap_area_t *vmap_area) {
    rb_erase(root,&vmap_area->rb_node,&empty_augment_callbacks);
    if (root == &free_vmap_area_root) update_subtree_max_size(vmap_area);
}
*/

//新建一个vmap_area
static vmap_area_t *create_vmap_area(UINT64 va_start,UINT64 va_end) {
    vmap_area_t *vmap_area=kmalloc(sizeof(vmap_area_t));
    vmap_area->va_start = va_start;
    vmap_area->va_end   = va_end;
    vmap_area->rb_node.parent_color = 0;
    vmap_area->rb_node.left   = NULL;
    vmap_area->rb_node.right  = NULL;
    vmap_area->list.prev = NULL;
    vmap_area->list.next = NULL;
    vmap_area->subtree_max_size = 0;
    return vmap_area;
}

/*//分割vmap_area
static vmap_area_t *split_vmap_area(vmap_area_t *vmap_area,UINT64 size) {
    vmap_area_t *new = create_vmap_area(vmap_area->va_start,vmap_area->va_start+size);
    vmap_area->va_start += size;
    vmap_area->va_end -= size;
    update_subtree_max_size(vmap_area);
    return new;
}*/

/*低地址优先搜索最佳适应空闲vmap_area*/
static vmap_area_t *find_vmap_lowest_match(UINT64 size,UINT64 va_start) {
    rb_node_t *node = free_vmap_area_root.rb_node;
    vmap_area_t *vmap_area = NULL;
    while (node) {
        vmap_area = CONTAINER_OF(node,vmap_area_t,rb_node);
        if (size <= get_subtree_max_size(node->left) && va_start <= vmap_area->va_start) {
            node=node->left;
        }else {
            if (size <= (vmap_area->va_end-va_start)) return vmap_area;
            node=node->right;
        }
    }
    return NULL;
}

/*分配一个vmap_area
 * 从free_vmap_area_root树上找一个最佳适应vmap_area
 * 节点大于size需要先分割，在把新的vmap_area插入used_vmap_area_root
 */
/*static vmap_area_t *alloc_vmap_area(UINT64 size,UINT64 va_start) {
    //找到最佳适应空闲节点
    vmap_area_t *vmap_area=find_vmap_lowest_match(size,va_start);
    if (!vmap_area)return NULL;

    UINT64 vmap_area_size = vmap_area->va_end-vmap_area->va_start;
    if (vmap_area_size == size) {
        del_vmap_area(&free_vmap_area_root,vmap_area);
    }else if (vmap_area_size > size) {
        vmap_area=split_vmap_area(vmap_area,size);
    }


}*/



/*计算最大值*/
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

static void vmap_area_augment_rotate(rb_node_t *old_node, rb_node_t *new_node) {
    vmap_area_t *old_vmap_area=CONTAINER_OF(old_node,vmap_area_t,rb_node);
    vmap_area_t *new_vmap_area=CONTAINER_OF(new_node,vmap_area_t,rb_node);
    //修正新节点的subtree_max_size
    new_vmap_area->subtree_max_size = old_vmap_area->subtree_max_size;
    //修正老节点的subtree_max_size
    compute_max(old_vmap_area,FALSE);
}

static void vmap_area_augment_copy(rb_node_t *old_node, rb_node_t *new_node) {
    vmap_area_t *old_vmap_area=CONTAINER_OF(old_node,vmap_area_t,rb_node);
    vmap_area_t *new_vmap_area=CONTAINER_OF(new_node,vmap_area_t,rb_node);
    //修正后继节点的subtree_max_size
    new_vmap_area->subtree_max_size = old_vmap_area->subtree_max_size;
}

static void vmap_area_augment_propagate(rb_node_t *start_node, rb_node_t *stop_node) {
    //向上修正subtree_max_size,当start_node=stop_node推出或者当前节点的subtree_max_size子树subtree_max_size一致时提前退出。
    while (start_node != stop_node) {
        vmap_area_t *vmap_area=CONTAINER_OF(start_node,vmap_area_t,rb_node);
        if (compute_max(vmap_area, TRUE)) break;
        start_node = rb_parent(start_node);
    }
}

//初始化vmalloc
void INIT_TEXT init_vmalloc(void) {
    vmap_area_augment_callbacks.rotate=vmap_area_augment_rotate;
    vmap_area_augment_callbacks.copy=vmap_area_augment_copy;
    vmap_area_augment_callbacks.propagate=vmap_area_augment_propagate;

    vmap_area_t *vmap_area;
    vmap_area=create_vmap_area(100,200);
    insert_vmap_area(&free_vmap_area_root,vmap_area,&vmap_area_augment_callbacks);

    vmap_area=create_vmap_area(500,700);
    insert_vmap_area(&free_vmap_area_root,vmap_area,&vmap_area_augment_callbacks);

    vmap_area=create_vmap_area(200,500);
    insert_vmap_area(&free_vmap_area_root,vmap_area,&vmap_area_augment_callbacks);



    //初始化vmalloc空间并插入空闲树
    // vmap_area_t *vmap_area=create_vmap_area(VMALLOC_START,VMALLOC_END);
    // insert_vmap_area(&free_vmap_area_root,vmap_area,&vmap_area_augment_callbacks);
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


