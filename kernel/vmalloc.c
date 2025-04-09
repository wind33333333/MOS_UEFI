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

//初始化vmalloc
void vmalloc_init(void) {
    //初始化vmalloc空间并插入空闲树
    vmap_area_t *new=new_vmap_area(VMALLOC_START,VMALLOC_END);
    insert_vmap_area(&free_vmap_area_root,new);
};

void *vmalloc (UINT64 size) {
    if (!size) return NULL;
    //4k对齐
    size = PAGE_4K_ALIGN(size);

    //找到最佳可用空闲节点
    vmap_area_t *vmap_area=find_vmap_lowest_match(size,VMALLOC_START);
    if (!vmap_area)return NULL;

    if (vmap_area->va_end-vmap_area->va_start > size) {
        split_vmap_area(vmap_area,size);
    }




}

// 更新当前节点的 subtree_max_size
void update_subtree_max_size(vmap_area_t *vmap_area) {
    UINT64 max_size = vmap_area->va_end - vmap_area->va_start; // 当前节点自身大小
    vmap_area_t *child = NULL;

    // 比较左子树的最大值
    if (vmap_area->rb_node.left) {
        child= CONTAINER_OF(vmap_area->rb_node.left,vmap_area_t,rb_node);
        if (max_size < child->subtree_max_size) max_size = child->subtree_max_size;
    }

    // 比较右子树的最大值
    if (vmap_area->rb_node.right) {
        child= CONTAINER_OF(vmap_area->rb_node.right,vmap_area_t,rb_node);
        if (max_size < child->subtree_max_size) max_size = child->subtree_max_size;
    }

    vmap_area->subtree_max_size = max_size; // 更新当前节点的子树最大值
}

//分割空闲节点
vmap_area_t *split_vmap_area(vmap_area_t *vmap_area,UINT64 size) {
    vmap_area_t *new = new_vmap_area(vmap_area->va_start,vmap_area->va_start+size);
    vmap_area->va_start += size;
    vmap_area->va_end -= size;
    return new;
}

UINT32 insert_vmap_area(rb_root_t *root, vmap_area_t *new_vmap_area) {
    rb_node_t **link = &root->rb_node;
    rb_node_t *father = NULL;
    vmap_area_t *entry;

    while (*link) {
        father = *link;
        entry = CONTAINER_OF(father,vmap_area_t,rb_node);
        if (new_vmap_area->va_start < entry->va_start) {
            link = &father->left;
        } else if (new_vmap_area->va_start > entry->va_start) {
            link = &father->right;
        } else {
            return 1;
        }
    }

    rb_link_node(&new_vmap_area->rb_node, father, link);
    rb_insert_color(root, &new_vmap_area->rb_node);
}

vmap_area_t *new_vmap_area(UINT64 va_start,UINT64 va_end) {
    vmap_area_t *new=kmalloc(sizeof(vmap_area_t));
    new->va_start = va_start;
    new->va_end   = va_end;
    new->rb_node.father_color = 0;
    new->rb_node.left   = NULL;
    new->rb_node.right  = NULL;
    new->list.prev = NULL;
    new->list.next = NULL;
    new->subtree_max_size = va_end - va_start;
    return new;
}

vmap_area_t *find_vmap_lowest_match(UINT64 size,UINT64 va_start) {
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
            while (cur_node->father != NULL && cur_node->father->right == cur_node) {
                cur_node = cur_node->father;
            }
            cur_node = cur_node->father;
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


