#include "../include/rbtree.h"

/* ========================================================================== */
/*                             核心基础操作：旋转                             */
/* ========================================================================== */

/**
 * @brief 红黑树左旋操作
 * @details 将 node 的右子节点提升为新父节点，node 降级为新父节点的左子节点。
 *          旋转过程中实时调用 augment_rotate 维护增强节点数据。
 */
static void rb_left_rotate(rb_root_t *root, rb_node_t *node, augment_rotate_f augment_rotate) {
    rb_node_t *parent = rb_parent(node);
    rb_node_t *new_parent = node->right;

    // 1. 新父节点的左子树，过继给原节点的右子树
    node->right = new_parent->left;
    if (new_parent->left) {
        rb_set_parent(new_parent->left, node);
    }

    // 2. 原节点降级为新父节点的左子节点
    new_parent->left = node;
    rb_set_parent(node, new_parent);
    rb_set_parent(new_parent, parent);

    // 3. 将新父节点接入原先的爷孙关系链
    if (!parent) {
        root->rb_node = new_parent;
    } else if (node == parent->left) {
        parent->left = new_parent;
    } else {
        parent->right = new_parent;
    }

    // 4. 触发回调，让上层业务更新这两个发生层级变化的节点增强数据
    augment_rotate(node, new_parent);
}

/**
 * @brief 红黑树右旋操作
 * @details 将 node 的左子节点提升为新父节点，node 降级为新父节点的右子节点。
 */
static void rb_right_rotate(rb_root_t *root, rb_node_t *node, augment_rotate_f augment_rotate) {
    rb_node_t *parent = rb_parent(node);
    rb_node_t *new_parent = node->left;

    // 1. 新父节点的右子树，过继给原节点的左子树
    node->left = new_parent->right;
    if (new_parent->right) {
        rb_set_parent(new_parent->right, node);
    }

    // 2. 原节点降级为新父节点的右子节点
    new_parent->right = node;
    rb_set_parent(node, new_parent);
    rb_set_parent(new_parent, parent);

    // 3. 将新父节点接入原先的爷孙关系链
    if (!parent) {
        root->rb_node = new_parent;
    } else if (node == parent->left) {
        parent->left = new_parent;
    } else {
        parent->right = new_parent;
    }

    // 4. 触发回调更新增强数据
    augment_rotate(node, new_parent);
}

/* ========================================================================== */
/*                             插入与平衡修正                                 */
/* ========================================================================== */

/**
 * @brief 修复红黑树插入后可能引发的连续红节点失衡
 */
static inline void rb_insert_fixup(rb_root_t *root, rb_node_t *node, augment_rotate_f augment_rotate) {
    rb_node_t *parent, *gparent, *uncle;

    // 触发条件：父节点存在且为红色（违反了红节点不能相连的特性）
    while ((parent = rb_parent(node)) && rb_is_red(parent)) {
        gparent = rb_parent(parent);

        // --- 镜像分支 A：父亲是祖父的左孩子 ---
        if (parent == gparent->left) {
            uncle = gparent->right;

            // Case 1: 叔叔是红色。通过变色将多余的红色向上传递。
            if (uncle && rb_is_red(uncle)) {
                rb_set_black(parent);
                rb_set_black(uncle);
                rb_set_red(gparent);
                node = gparent; // 上推至祖父继续检测
            } else {
                // Case 2: 叔叔是黑色，且当前节点是右孩子 (LR 型)
                if (node == parent->right) {
                    rb_left_rotate(root, parent, augment_rotate);
                    node = parent;
                    parent = rb_parent(node); // 旋转后，原父节点降级，重新对齐指针准备进入 Case 3
                }
                // Case 3: 叔叔是黑色，且当前节点是左孩子 (LL 型)
                rb_right_rotate(root, gparent, augment_rotate);
                rb_set_black(parent);
                rb_set_red(gparent);
            }
        }
        // --- 镜像分支 B：父亲是祖父的右孩子 ---
        else {
            uncle = gparent->left;

            // Case 1: 叔叔是红色
            if (uncle && rb_is_red(uncle)) {
                rb_set_black(parent);
                rb_set_black(uncle);
                rb_set_red(gparent);
                node = gparent;
            } else {
                // Case 2: 叔叔是黑色，且当前节点是左孩子 (RL 型)
                if (node == parent->left) {
                    rb_right_rotate(root, parent, augment_rotate);
                    node = parent;
                    parent = rb_parent(node);
                }
                // Case 3: 叔叔是黑色，且当前节点是右孩子 (RR 型)
                rb_left_rotate(root, gparent, augment_rotate);
                rb_set_black(parent);
                rb_set_red(gparent);
            }
        }
    }
    // 铁律：根节点永远为黑
    rb_set_black(root->rb_node);
}

/**
 * @brief 将游离节点物理链入红黑树
 */
static inline void rb_link_node(rb_node_t *node, rb_node_t *parent, rb_node_t **link) {
    node->parent_color = (uint64)parent; // 新节点默认颜色必定为 0 (红色)
    node->left = NULL;
    node->right = NULL;
    *link = node;
}

/**
 * @brief 插入红黑树总控接口
 */
void rb_insert(rb_root_t *root, rb_node_t *node, rb_node_t *parent, rb_node_t **link, rb_augment_callbacks_f *augment_callbacks) {
    rb_link_node(node, parent, link);

    // 🌟 修复 Bug: 必须在破坏平衡(旋转)前，立刻沿着当前正确的爷孙关系链向上更新增强数据！
    augment_callbacks->propagate(node, NULL);

    // 执行底层的红黑树颜色与结构调整
    rb_insert_fixup(root, node, augment_callbacks->rotate);
}

/* ========================================================================== */
/*                             删除与平衡修正                                 */
/* ========================================================================== */

/**
 * @brief 修复红黑树删除黑色节点导致的黑高失衡
 */
static inline void rb_erase_fixup(rb_root_t *root, rb_node_t *node, rb_node_t *parent, augment_rotate_f augment_rotate) {
    rb_node_t *sibling;

    // 当遇到红色节点（直接染黑补偿）或到达根节点时，黑高恢复，停止循环
    while (node != root->rb_node && (!node || rb_is_black(node))) {

        // --- 镜像分支 A：被删节点位于左侧，兄弟在右侧 ---
        if (node == parent->left) {
            sibling = parent->right;

            // Case 1: 兄弟为红色。左旋父亲，使得新兄弟必定为黑色，降级为情况 2/3/4 处理。
            if (rb_is_red(sibling)) {
                rb_set_black(sibling);
                rb_set_red(parent);
                rb_left_rotate(root, parent, augment_rotate);
                sibling = parent->right;
            }

            // Case 2: 兄弟为黑，且其左右双子均为黑。将黑高缺失向上推移一层。
            if ((!sibling->left || rb_is_black(sibling->left)) &&
                (!sibling->right || rb_is_black(sibling->right))) {
                rb_set_red(sibling);
                node = parent;
                parent = rb_parent(node);
            } else {
                // Case 3: 兄弟为黑，且右子为黑（必定左子为红）。RL 转换为 RR 型。
                if (!sibling->right || rb_is_black(sibling->right)) {
                    rb_set_black(sibling->left); // 🌟 致命 Bug 修复：必须在此反转颜色！
                    rb_set_red(sibling);         // 🌟 致命 Bug 修复：否则旋转后黑高丢失！
                    rb_right_rotate(root, sibling, augment_rotate);
                    sibling = parent->right;
                }
                // Case 4: 兄弟为黑，且右子为红 (RR 型)。一记左旋，彻底填补黑高！
                rb_set_color(sibling, rb_color(parent));
                rb_set_black(parent);
                rb_set_black(sibling->right);
                rb_left_rotate(root, parent, augment_rotate);
                break; // 填补完成，全局平衡恢复
            }
        }
        // --- 镜像分支 B：被删节点位于右侧，兄弟在左侧 ---
        else {
            sibling = parent->left;

            // Case 1: 兄弟为红色
            if (rb_is_red(sibling)) {
                rb_set_black(sibling);
                rb_set_red(parent);
                rb_right_rotate(root, parent, augment_rotate);
                sibling = parent->left;
            }

            // Case 2: 兄弟为黑，双子均为黑
            if ((!sibling->left || rb_is_black(sibling->left)) &&
                (!sibling->right || rb_is_black(sibling->right))) {
                rb_set_red(sibling);
                node = parent;
                parent = rb_parent(node);
            } else {
                // Case 3: 兄弟为黑，左子为黑（右子为红）。LR 转换为 LL 型。
                if (!sibling->left || rb_is_black(sibling->left)) {
                    rb_set_black(sibling->right); // 🌟 致命 Bug 修复
                    rb_set_red(sibling);          // 🌟 致命 Bug 修复
                    rb_left_rotate(root, sibling, augment_rotate);
                    sibling = parent->left;
                }
                // Case 4: 兄弟为黑，左子为红 (LL 型)。
                rb_set_color(sibling, rb_color(parent));
                rb_set_black(parent);
                rb_set_black(sibling->left);
                rb_right_rotate(root, parent, augment_rotate);
                break;
            }
        }
    }
    if (node) rb_set_black(node);
}

/**
 * @brief 执行节点的物理剥离与后继节点替换
 * @return 导出失衡点的 parent、child 和待补偿的 color，供 fixup 使用
 */
static inline void rb_replace_erase(rb_root_t *root, rb_node_t *node, augment_copy_f augment_copy,
                                    rb_node_t **out_parent, rb_node_t **out_child, rb_color_e *out_color) {
    rb_node_t *parent, *child;
    rb_color_e color;

    // 情况 1: 具备双子节点，必须提取右子树的极左节点 (后继者) 进行位置替换
    if (node->left && node->right) {
        rb_node_t *successor = node->right;
        while (successor->left) successor = successor->left;

        // 触发回调：将旧节点的增强数据 (如区间限制) 复制给替身节点
        augment_copy(node, successor);

        parent = successor;
        child = successor->right;
        color = rb_color(successor); // 我们实际物理剔除的是 successor 节点，故记录其颜色

        if (successor != node->right) {
            parent = rb_parent(successor);
            if (child) rb_set_parent(child, parent);

            parent->left = child;
            successor->right = node->right;
            rb_set_parent(successor->right, successor);
        }

        successor->left = node->left;
        rb_set_parent(node->left, successor);
        // 替身节点继承被删节点的颜色和身份
        rb_set_parent_and_color(successor, rb_parent(node), rb_color(node));

        if (rb_parent(node)) {
            if (node == rb_parent(node)->left) rb_parent(node)->left = successor;
            else rb_parent(node)->right = successor;
        } else {
            root->rb_node = successor;
        }
    }
    // 情况 2: 只有单边子树，或作为叶子节点，直接提拔独子 (或 NULL)
    else {
        child = node->left ? node->left : node->right;
        parent = rb_parent(node);
        color = rb_color(node);

        if (child) rb_set_parent(child, parent);

        if (parent) {
            if (node == parent->left) parent->left = child;
            else parent->right = child;
        } else {
            root->rb_node = child;
        }
    }

    *out_parent = parent;
    *out_child = child;
    *out_color = color;
}

/**
 * @brief 删除红黑树节点总控接口
 */
void rb_erase(rb_root_t *root, rb_node_t *node, rb_augment_callbacks_f *augment_callbacks) {
    rb_node_t *parent, *child;
    rb_color_e color;

    // 1. 物理移除并用后继节点填补空缺
    rb_replace_erase(root, node, augment_callbacks->copy, &parent, &child, &color);

    // 2. 🌟 修复 Bug: 物理剥离完毕后、旋转洗牌发生前，立刻向上修正增强数据！
    // 此时的 parent 指向实际物理移除点（被删节点或其后继节点）的原始父亲。
    if (parent) {
        augment_callbacks->propagate(parent, NULL);
    }

    // 3. 若物理移除的是黑色节点，触发底层的黑高补偿修正
    if (color == rb_black) {
        rb_erase_fixup(root, child, parent, augment_callbacks->rotate);
    }
}

/* ========================================================================== */
/*                             空回调防护机制                                 */
/* ========================================================================== */

static void empty_augment_rotate(rb_node_t *old_node, rb_node_t *new_node) {}
static void empty_augment_copy(rb_node_t *old_node, rb_node_t *new_node) {}
static void empty_augment_propagate(rb_node_t *start_node, rb_node_t *stop_node) {}

rb_augment_callbacks_f empty_augment_callbacks;

void INIT_TEXT rbtree_empty_augment_callbacks_init(void) {
    empty_augment_callbacks.rotate    = empty_augment_rotate;
    empty_augment_callbacks.copy      = empty_augment_copy;
    empty_augment_callbacks.propagate = empty_augment_propagate;
}