#pragma once
#include "moslib.h"

// 基础双向链表结构
typedef struct list_head {
    struct list_head *next;
    struct list_head *prev;
} list_head_t;

/**
 * 内部核心辅助函数：在已知的两个相邻节点之间插入新节点
 * @param new_node 要插入的新节点
 * @param prev     前驱节点
 * @param next     后继节点
 */
static inline void __list_add(list_head_t *new_node, list_head_t *prev, list_head_t *next) {
    next->prev = new_node;
    new_node->next = next;
    new_node->prev = prev;
    prev->next = new_node;
}

/**
 * 将新节点插入到指定节点 (head) 的正后方 (即头插法/紧随其后)
 */
static inline void list_add(list_head_t *new_node, list_head_t *head) {
    __list_add(new_node, head, head->next);
}

/**
 * 将新节点插入到指定节点 (head) 的正前方 (即链表尾部)
 */
static inline void list_add_tail(list_head_t *new_node, list_head_t *head) {
    __list_add(new_node, head->prev, head);
}


static inline void list_del(list_head_t *node) {
    node->next->prev = node->prev;
    node->prev->next = node->next;
}

static inline void list_head_init(list_head_t *head) {
    head->prev = head;
    head->next = head;
}

/**
 * 遍历双向链表
 */
#define list_for_each(pos, head) \
for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * 安全遍历双向链表 (允许在循环内调用 list_del 删除当前节点)
 */
#define list_for_each_safe(pos, n, head) \
for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)

/**
 * 将节点从链表中安全摘除，并将其重新初始化为空节点
 */
static inline void list_del_init(list_head_t *node) {
    list_del(node);
    list_head_init(node);
}

/**
 * 🌟 优化：复用宏，让代码更优雅
 */
static inline boolean list_find( list_head_t *node,list_head_t *head) {
    list_head_t *pos;
    list_for_each(pos, head) {
        if (pos == node) {
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * 🌟 致命 Bug 修复：循环链表判空必须是判断是否指向自己
 */
static inline boolean list_empty(const list_head_t *head) {
    return head->next == head;
}