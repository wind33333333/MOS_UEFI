#include "moslib.h"

/*
 * 链表操作函数
 */
typedef struct list_head_t{
    struct list_head_t *prev;
    struct list_head_t *next;
}list_head_t;

static inline void list_add_head(list_head_t *head,list_head_t *new) {
    new->next = head->next;  // 新节点指向原头节点
    new->prev = head;        // 新节点前驱指向头节点
    head->next->prev = new;  // 原头节点的前驱指向新节点
    head->next = new;        // 头节点指向新节点
}

static inline void list_add_tail(list_head_t *head,list_head_t *new) {
    new->next = head;        // 新节点指向头节点（循环）
    new->prev = head->prev;  // 新节点前驱指向原尾节点
    head->prev->next = new;  // 原尾节点的后继指向新节点
    head->prev = new;        // 头节点的前驱指向新节点
}

static inline void list_del(list_head_t * node)
{
    node->next->prev = node->prev;
    node->prev->next = node->next;
}

static inline void list_head_init(list_head_t *head) {
    head->prev = head;
    head->next = head;
}

/**
 * @brief 遍历双向链表
 * @param pos  用来当作迭代游标的 list_head_t * 指针
 * @param head 链表的头节点
 */
#define list_for_each(pos, head) \
for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * @brief 安全遍历双向链表 (允许在循环内删除节点)
 * @param pos  用来当作迭代游标的 list_head_t * 指针
 * @param n    用来做临时备份的 list_head_t * 指针 (★ 核心防线)
 * @param head 链表的头节点
 */
#define list_for_each_safe(pos, n, head) \
for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)


/**
 * @brief 将节点从链表中安全摘除，并将其重新初始化为空节点
 * @param node 要摘除的节点
 */
static inline void list_del_init(list_head_t *node) {
    // 1. 让它的前驱和后继互相牵手 (把它踢出群聊)
    list_del(node);

    // 2. 🌟 关键动作：让它自己牵自己的手！
    list_head_init(node);
}

static inline boolean list_find(list_head_t *head,list_head_t *node) {
    list_head_t *next = head;
    while (next->next != head) {
        if (next->next == node)
            return TRUE;
        next=next->next;
    }
    return FALSE;
}

static inline boolean list_empty(list_head_t *head) {
    if (head->next == NULL && head->prev == NULL)
        return TRUE;
    return FALSE;
}