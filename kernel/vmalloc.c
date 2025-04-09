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
    vmap_area_t *vmap_area=create_vmap_area(VMALLOC_START,VMALLOC_END);
    insert_vmap_area(&free_vmap_area_root,vmap_area);
};

void *vmalloc (UINT64 size) {
    if (!size) return NULL;
    //4k对齐
    size = PAGE_4K_ALIGN(size);

    //找到最佳可用空闲节点
    vmap_area_t *vmap_area=find_vmap_lowest_match(size,VMALLOC_START);
    if (!vmap_area)return NULL;

    UINT64 vmap_area_size = vmap_area->va_end-vmap_area->va_start;
    if (vmap_area_size == size) {

    }else if (vmap_area_size > size) {
        split_vmap_area(vmap_area,size);
    }




}

// 更新的subtree_max_size
void update_subtree_max_size(vmap_area_t *vmap_area) {
    vmap_area_t *child;
    rb_node_t *node = &vmap_area->rb_node;
     do{
        vmap_area->subtree_max_size = vmap_area->va_end - vmap_area->va_start; // 当前节点自身大小
        // 比较左子树的取最大值
        if (node->left) {
            child= CONTAINER_OF(node->left,vmap_area_t,rb_node);
            if (vmap_area->subtree_max_size < child->subtree_max_size)
                vmap_area->subtree_max_size = child->subtree_max_size;
        }
        // 比较右子树的取最大值
        if (node->right) {
            child= CONTAINER_OF(node->right,vmap_area_t,rb_node);
            if (vmap_area->subtree_max_size < child->subtree_max_size)
                vmap_area->subtree_max_size = child->subtree_max_size;
        }
        node = rb_parent(node);
        vmap_area = CONTAINER_OF(node,vmap_area_t,rb_node);
    }while (node);
}

//分割空闲节点
vmap_area_t *split_vmap_area(vmap_area_t *vmap_area,UINT64 size) {
    vmap_area_t *new = create_vmap_area(vmap_area->va_start,vmap_area->va_start+size);
    vmap_area->va_start += size;
    vmap_area->va_end -= size;
    update_subtree_max_size(vmap_area);
    return new;
}

//插入vmap
UINT32 insert_vmap_area(rb_root_t *root, vmap_area_t *vmap_area) {
    rb_node_t *parent,**link = &root->rb_node;
    vmap_area_t *cur_vmap_area;

    while ((parent = *link)) {
        cur_vmap_area = CONTAINER_OF(parent,vmap_area_t,rb_node);
        if (vmap_area->va_start < cur_vmap_area->va_start) {
            link = &parent->left;
        } else if (vmap_area->va_start > cur_vmap_area->va_start) {
            link = &parent->right;
        } else {
            return 1;
        }
    }

    rb_link_node(&vmap_area->rb_node, parent, link);
    rb_insert_color(root, &vmap_area->rb_node);
    if (root == &free_vmap_area_root) update_subtree_max_size(vmap_area);
    return 0;
}

//新建一个vmap
vmap_area_t *create_vmap_area(UINT64 va_start,UINT64 va_end) {
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

//搜索最佳空闲vmap
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


