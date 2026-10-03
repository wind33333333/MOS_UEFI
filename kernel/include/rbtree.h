#pragma once

#include "moslib.h"

typedef struct rb_node_t {
    uint64 parent_color; //父节点和颜色
    struct rb_node_t *left; //左子节点
    struct rb_node_t *right; //右子节点
} rb_node_t;

typedef struct rb_root_t {
    rb_node_t *rb_node; //树根
                        //锁
} rb_root_t;


// 获取父节点（清除颜色位）
static inline rb_node_t *rb_parent(const rb_node_t *node) {
    return (rb_node_t *) (node->parent_color & ~1UL);
}



// 增强数据旋转回调函数类型
typedef void (*augment_rotate_f) (rb_node_t *old_node, rb_node_t *new_node);

// 增强数据复制回调函数类型
typedef void (*augment_copy_f) (rb_node_t *old_node, rb_node_t *new_node);

// 曾将数据向上更新回调函数类型
typedef void (*augment_propagate_f) (rb_node_t *start_node, rb_node_t *stop_node);

typedef struct {
    augment_propagate_f propagate;
    augment_copy_f copy;
    augment_rotate_f rotate;
}rb_augment_callbacks_f;



void rb_erase(rb_root_t *root, rb_node_t *node,rb_augment_callbacks_f *augment_callbacks);          //红黑树删除操作
void rb_insert(rb_root_t *root, rb_node_t *node, rb_node_t *parent, rb_node_t **link, rb_augment_callbacks_f *augment_callbacks); //红黑树插入操作

