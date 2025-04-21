#ifndef RBTREE_H
#define RBTREE_H
#include "moslib.h"

typedef enum {
    rb_red = 0,
    rb_black = 1,
} rb_color_e;


typedef struct rb_node_t {
    UINT64 parent_color; //父节点和颜色
    struct rb_node_t *left; //左子节点
    struct rb_node_t *right; //右子节点
} rb_node_t;

typedef struct rb_root_t {
    rb_node_t *rb_node; //树根
} rb_root_t;


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

// 获取节点颜色（0为红，1为黑）
static inline UINT32 rb_color(const rb_node_t *node) {
    return node->parent_color & 1;
}

// 获取父节点（清除颜色位）
static inline rb_node_t *rb_parent(const rb_node_t *node) {
    return (rb_node_t *) (node->parent_color & ~1UL);
}

// 设置父节点（保留原有颜色）
static inline void rb_set_parent(rb_node_t *node, rb_node_t *parent) {
    node->parent_color = (UINT64) parent | rb_color(node);
}

// 判断是否为红色（颜色位为 0）
static inline BOOLEAN rb_is_red(const rb_node_t *node) {
    return !rb_color(node);
}

// 判断是否为黑色（颜色位为 1）
static inline BOOLEAN rb_is_black(const rb_node_t *node) {
    return rb_color(node);
}

// 设置为红色（清除颜色位后设为 0）
static inline void rb_set_red(rb_node_t *node) {
    node->parent_color &= ~1UL; // ~1UL = 0xFFFF...FE，清除最低位
}

// 设置为黑色（保留父指针，设置颜色位为 1）
static inline void rb_set_black(rb_node_t *node) {
    node->parent_color |= 1UL;
}

// 通用颜色设置函数（color 需为 RB_RED 或 RB_BLACK）
static inline void rb_set_color(rb_node_t *node, UINT32 color) {
    node->parent_color = (node->parent_color & ~1UL) | color;
}

/*------------ 高级组合操作 ------------*/
// 同时设置父节点和颜色（初始化或重链接时使用）
static inline void rb_set_parent_and_color(rb_node_t *node, rb_node_t *parent, UINT32 color) {
    node->parent_color = (UINT64) parent | color;
}

extern rb_augment_callbacks_f empty_augment_callbacks;
void init_rbtree(void);
void rb_erase(rb_root_t *root, rb_node_t *node,rb_augment_callbacks_f *augment_callbacks);
void rb_insert(rb_root_t *root, rb_node_t *node, rb_node_t *parent, rb_node_t **link, rb_augment_callbacks_f *augment_callbacks);


#endif