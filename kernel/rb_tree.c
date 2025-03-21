/*
 * 红黑树删除后重平衡核心逻辑
 * node:    当前需要调整的节点（可能为NULL）
 * parent:  node的父节点
 * root:    树的根节点
 */
static void __rb_erase_color(struct rb_node *node, struct rb_node *parent, struct rb_root *root)
{
    struct rb_node *sibling;

    // 循环处理，直到node是根节点或node变为红色
    while (node != root->rb_node && rb_is_black(node)) {
        if (node == parent->rb_left) { // 当前节点是父节点的左子节点
            sibling = parent->rb_right; // 兄弟节点为父节点的右子节点

            /* ------------- Case 1: 兄弟节点为红色 ------------- */
            if (rb_is_red(sibling)) {
                // 将兄弟节点染黑，父节点染红
                rb_set_black(sibling);
                rb_set_red(parent);
                // 对父节点进行左旋，使兄弟节点成为新的父节点
                __rb_rotate_left(parent, root);
                sibling = parent->rb_right; // 更新兄弟节点为原兄弟的左子节点
            }

            /* ---- Case 2: 兄弟节点为黑，且其子节点均为黑 ---- */
            if (rb_is_black(sibling->rb_left) && rb_is_black(sibling->rb_right)) {
                rb_set_red(sibling); // 兄弟节点染红
                node = parent;       // 问题上移至父节点
                parent = rb_parent(node); // 更新父指针
            } else {
                /* ---- Case 3: 兄弟节点为黑，且远侄子为红 ---- */
                if (rb_is_black(sibling->rb_right)) {
                    // 将兄弟节点的左子节点染黑，兄弟节点染红
                    rb_set_black(sibling->rb_left);
                    rb_set_red(sibling);
                    // 对兄弟节点进行右旋，转换为Case 4
                    __rb_rotate_right(sibling, root);
                    sibling = parent->rb_right; // 更新兄弟节点
                }

                // 此时兄弟的右子节点为红（Case 4）
                rb_set_color(sibling, rb_color(parent)); // 继承父节点颜色
                rb_set_black(parent);                   // 父节点染黑
                rb_set_black(sibling->rb_right);        // 兄弟右子染黑
                __rb_rotate_left(parent, root);         // 左旋父节点
                node = root->rb_node; // 调整完成，退出循环
                break;
            }
        } else { // 对称情况：当前节点是父节点的右子节点
            sibling = parent->rb_left;

            // Case 1镜像：兄弟节点为红
            if (rb_is_red(sibling)) {
                rb_set_black(sibling);
                rb_set_red(parent);
                __rb_rotate_right(parent, root);
                sibling = parent->rb_left;
            }

            // Case 2镜像：兄弟子节点均为黑
            if (rb_is_black(sibling->rb_left) && rb_is_black(sibling->rb_right)) {
                rb_set_red(sibling);
                node = parent;
                parent = rb_parent(node);
            } else {
                // Case 3镜像：近侄子为红
                if (rb_is_black(sibling->rb_left)) {
                    rb_set_black(sibling->rb_right);
                    rb_set_red(sibling);
                    __rb_rotate_left(sibling, root);
                    sibling = parent->rb_left;
                }

                // Case 4镜像：远侄子为红
                rb_set_color(sibling, rb_color(parent));
                rb_set_black(parent);
                rb_set_black(sibling->rb_left);
                __rb_rotate_right(parent, root);
                node = root->rb_node;
                break;
            }
        }
    }

    // 最终确保根节点为黑（性质2）
    if (node)
        rb_set_black(node);
}


/*
 * 红黑树删除主逻辑
 * 注意：被删除节点必须已存在于树中
 */
void rb_erase(struct rb_node *node, struct rb_root *root)
{
    struct rb_node *rebalance = NULL; // 重平衡起始节点
    struct rb_node *child;            // 被删除节点的子节点
    struct rb_node *parent;           // 被删除节点的父节点
    int color;                        // 被删除节点的原始颜色

    /* -------------------- 第1阶段：二叉搜索树删除 -------------------- */
    if (!node->rb_left) {             // Case 1: 左子节点为空
        child = node->rb_right;
    } else if (!node->rb_right) {     // Case 2: 右子节点为空
        child = node->rb_left;
    } else {                          // Case 3: 左右子节点均存在
        struct rb_node *old = node;   // 保存原始节点指针
        struct rb_node *left;         // 后继节点的左子节点

        node = node->rb_right;        // 移动到右子树
        while ((left = node->rb_left) != NULL)
            node = left;              // 找到右子树的最小节点（后继节点）

        child = node->rb_right;       // 后继节点的右子节点（可能为空）
        parent = rb_parent(node);     // 后继节点的父节点
        color = rb_color(node);       // 后继节点的颜色

        if (child)                    // 如果后继节点有右子节点
            rb_set_parent(child, parent); // 更新右子节点的父指针

        if (parent == old) {          // 后继节点是原始节点的直接右子节点
            parent->rb_right = child;
            parent = node;            // 调整parent指向后继节点
        } else {
            parent->rb_left = child;  // 将后继节点的右子树挂到父节点左子树
        }

        node->rb_left = old->rb_left; // 将原始节点的左子树转移给后继节点
        rb_set_parent(old->rb_left, node);

        node->rb_right = old->rb_right; // 处理右子树
        rb_set_parent(old->rb_right, node);

        node->rb_parent_color = old->rb_parent_color; // 继承颜色和父指针
        node->rb_left = old->rb_left;                 // 冗余赋值，确保正确性

        if (rb_parent(old)) {         // 更新原始节点父节点的子指针
            if (rb_parent(old)->rb_left == old)
                rb_parent(old)->rb_left = node;
            else
                rb_parent(old)->rb_right = node;
        } else {
            root->rb_node = node;     // 若删除的是根节点，更新根指针
        }

        rebalance = (color == RB_BLACK) ? child : NULL; // 仅当后继节点原为黑色时需要重平衡
        goto color_corrected;         // 跳转到颜色修正阶段
    }

    /* -------------------- 处理简单情况（0或1个子节点） ----------------- */
    parent = rb_parent(node);         // 获取父节点
    color = rb_color(node);           // 获取节点颜色

    if (child)                        // 如果存在子节点
        rb_set_parent(child, parent); // 更新子节点的父指针

    if (parent) {                     // 更新父节点的子指针
        if (parent->rb_left == node)
            parent->rb_left = child;
        else
            parent->rb_right = child;
    } else {
        root->rb_node = child;        // 若删除的是根节点，更新根指针
    }

    rebalance = (color == RB_BLACK) ? child : NULL; // 仅当删除黑色节点时需要重平衡

color_corrected:
    /* -------------------- 第2阶段：红黑树重平衡 -------------------- */
    if (rebalance)
        __rb_erase_color(rebalance, parent, root); // 调用内部平衡函数
}

#include <stddef.h> // 用于 NULL 定义

/* 红黑树节点结构（仿 Linux 内核设计） */
struct rb_node {
    unsigned long  __rb_parent_color; // 父指针 + 颜色（低2位）
    struct rb_node *rb_right;
    struct rb_node *rb_left;
} __attribute__((aligned(sizeof(long)))); // 确保对齐到 long 类型大小

/* 颜色常量定义 */
#define RB_RED   0
#define RB_BLACK 1

/*------------ 基础操作函数 ------------*/
// 获取父节点（清除颜色位）
static inline struct rb_node *rb_parent(const struct rb_node *node) {
    return (struct rb_node *)(node->__rb_parent_color & ~3UL);
}

// 设置父节点（保留原有颜色）
static inline void rb_set_parent(struct rb_node *node, struct rb_node *parent) {
    node->__rb_parent_color = (unsigned long)parent | (node->__rb_parent_color & 3UL);
}

/*------------ 颜色操作函数 ------------*/
// 判断是否为红色（颜色位为 0）
static inline int rb_is_red(const struct rb_node *node) {
    return (node->__rb_parent_color & 1) == RB_RED;
}

// 判断是否为黑色（颜色位为 1）
static inline int rb_is_black(const struct rb_node *node) {
    return (node->__rb_parent_color & 1) == RB_BLACK;
}

// 设置为红色（清除颜色位后设为 0）
static inline void rb_set_red(struct rb_node *node) {
    node->__rb_parent_color &= ~1UL; // ~1UL = 0xFFFF...FE，清除最低位
}

// 设置为黑色（保留父指针，设置颜色位为 1）
static inline void rb_set_black(struct rb_node *node) {
    node->__rb_parent_color |= 1UL;
}

// 通用颜色设置函数（color 需为 RB_RED 或 RB_BLACK）
static inline void rb_set_color(struct rb_node *node, int color) {
    node->__rb_parent_color = (node->__rb_parent_color & ~1UL) | (color & 1);
}

/*------------ 高级组合操作 ------------*/
// 同时设置父节点和颜色（初始化或重链接时使用）
static inline void rb_set_parent_and_color(struct rb_node *node,
                                          struct rb_node *parent,
                                          int color) {
    node->__rb_parent_color = (unsigned long)parent | (color & 1);
}

// 全局定义红黑树根（初始为空树）
// RB_ROOT 宏初始化根节点指针为NULL
struct rb_root my_tree_root = RB_ROOT;

// 插入业务数据到红黑树
// 参数: new_data - 要插入的新数据节点（包含嵌入的红黑树节点）
// 返回: 0成功，-EEXIST表示ID冲突
int insert_my_data(struct my_data *new_data) {
    // 定义双指针用于遍历树（link总指向当前节点的左/右子节点指针的地址）
    struct rb_node ​**link = &my_tree_root.rb_node; // 从根节点开始查找
    struct rb_node *parent = NULL;        // 记录父节点位置
    struct my_data *entry;                // 用于暂存当前节点的业务数据

    /* 阶段1：二叉搜索树插入定位 */
    // 循环查找合适的插入位置（平均时间复杂度O(log n)）
    while (*link) { // 当当前节点不为空时继续查找
        parent = *link; // 记录父节点指针
        // 通过rb_entry宏从rb_node指针获取外层业务数据结构
        // 参数分解：parent - rb_node指针, struct my_data - 外层类型, node - 嵌入的成员名
        entry = rb_entry(parent, struct my_data, node);

        // 比较键值决定遍历方向（根据业务数据ID字段）
        if (new_data->id < entry->id)         // 新ID较小，向左子树查找
            link = &parent->rb_left;         // 更新link为左子节点指针的地址
        else if (new_data->id > entry->id)   // 新ID较大，向右子树查找
            link = &parent->rb_right;        // 更新link为右子节点指针的地址
        else                                  // ID已存在，返回冲突错误
            return -EEXIST; // 返回错误码（典型错误号，如-17表示已存在）
    }

    /* 阶段2：节点链接与红黑树平衡 */
    // 将新节点连接到找到的位置（此时*link为NULL，表示父节点的空子节点位置）
    // 参数分解：
    // 1. &new_data->node - 新节点的rb_node指针
    // 2. parent - 找到的父节点
    // 3. link - 指向父节点左/右子节点指针的地址
    rb_link_node(&new_data->node, parent, link);

    // 执行红黑树再平衡操作（修正颜色和旋转，保持红黑树性质）
    // 参数分解：
    // 1. &new_data->node - 新插入的节点
    // 2. &my_tree_root - 树根指针地址（可能需要更新根节点）
    rb_insert_color(&new_data->node, &my_tree_root);

    return 0; // 插入成功
}