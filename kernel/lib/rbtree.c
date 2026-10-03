#include "../include/rbtree.h"

typedef enum {
    rb_red = 0,
    rb_black = 1,
} rb_color_e;




// 获取节点颜色（0为红，1为黑）
static inline uint32 rb_color(const rb_node_t *node) {
    return node->parent_color & 1;
}


// 设置父节点（保留原有颜色）
static inline void rb_set_parent(rb_node_t *node, rb_node_t *parent) {
    node->parent_color = (uint64) parent | rb_color(node);
}

// 判断是否为红色（颜色位为 0）
static inline boolean rb_is_red(const rb_node_t *node) {
    return !rb_color(node);
}

// 判断是否为黑色（颜色位为 1）
static inline boolean rb_is_black(const rb_node_t *node) {
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
static inline void rb_set_color(rb_node_t *node, uint32 color) {
    node->parent_color = (node->parent_color & ~1UL) | color;
}

/*------------ 高级组合操作 ------------*/
// 同时设置父节点和颜色（初始化或重链接时使用）
static inline void rb_set_parent_and_color(rb_node_t *node, rb_node_t *parent, uint32 color) {
    node->parent_color = (uint64) parent | color;
}


/* ========================================================================== */
/*                            核心基础操作：旋转                              */
/* ========================================================================== */

/**
 * @brief 红黑树左旋操作
 * @details 将 node 的右子节点提升为新父节点，node 降级为新父节点的左子节点。
 *          若提供了 augment_rotate 回调，则在物理旋转完成后立刻修正增强数据。
 */
static void rb_left_rotate(rb_root_t *root, rb_node_t *node, augment_rotate_f augment_rotate) {
    rb_node_t *parent = rb_parent(node);
    rb_node_t *new_parent = node->right;

    // 1. 移交子树：新父节点的左子树，过继给原节点的右子树
    node->right = new_parent->left;
    if (new_parent->left) {
        rb_set_parent(new_parent->left, node);
    }

    // 2. 节点降级：原节点降级为新父节点的左子节点
    new_parent->left = node;
    rb_set_parent(node, new_parent);
    rb_set_parent(new_parent, parent); // 继承原本的祖父关系

    // 3. 接入全局：将新父节点接入原先的爷孙关系链
    if (!parent) {
        root->rb_node = new_parent; // 如果原节点是根，新父节点成为新根
    } else if (node == parent->left) {
        parent->left = new_parent;
    } else {
        parent->right = new_parent;
    }

    // 4. 🌟 增强数据修正：利用分支预测器实现零开销调用
    if (augment_rotate) {
        augment_rotate(node, new_parent);
    }
}

/**
 * @brief 红黑树右旋操作
 * @details 将 node 的左子节点提升为新父节点，node 降级为新父节点的右子节点。
 */
static void rb_right_rotate(rb_root_t *root, rb_node_t *node, augment_rotate_f augment_rotate) {
    rb_node_t *parent = rb_parent(node);
    rb_node_t *new_parent = node->left;

    // 1. 移交子树：新父节点的右子树，过继给原节点的左子树
    node->left = new_parent->right;
    if (new_parent->right) {
        rb_set_parent(new_parent->right, node);
    }

    // 2. 节点降级：原节点降级为新父节点的右子节点
    new_parent->right = node;
    rb_set_parent(node, new_parent);
    rb_set_parent(new_parent, parent);

    // 3. 接入全局：将新父节点接入原先的爷孙关系链
    if (!parent) {
        root->rb_node = new_parent;
    } else if (node == parent->left) {
        parent->left = new_parent;
    } else {
        parent->right = new_parent;
    }

    // 4. 🌟 增强数据修正
    if (augment_rotate) {
        augment_rotate(node, new_parent);
    }
}

/* ========================================================================== */
/*                             插入与平衡修正                                 */
/* ========================================================================== */

/**
 * @brief 修复红黑树插入后可能引发的连续红节点失衡 (红红相连)
 */
static inline void rb_insert_fixup(rb_root_t *root, rb_node_t *node, augment_rotate_f augment_rotate) {
    rb_node_t *parent, *gparent, *uncle;

    // 触发条件：父节点存在且为红色（违反了红节点不能相连的特性）
    while ((parent = rb_parent(node)) && rb_is_red(parent)) {
        gparent = rb_parent(parent); // 父节点是红，必定不是根，故祖父必然存在

        // --- 镜像分支 A：父亲是祖父的左孩子 ---
        if (parent == gparent->left) {
            uncle = gparent->right;

            // Case 1: 叔叔是红色。通过变色将多余的红色向上传递。
            if (uncle && rb_is_red(uncle)) {
                rb_set_black(parent);
                rb_set_black(uncle);
                rb_set_red(gparent);
                node = gparent; // 上推至祖父，继续下一轮检测
            } else {
                // Case 2: 叔叔是黑色，且当前节点是右孩子 (LR 型折线)
                if (node == parent->right) {
                    rb_left_rotate(root, parent, augment_rotate);
                    node = parent;
                    parent = rb_parent(node); // 旋转后，原父节点降级，重新对齐指针准备进入 Case 3
                }
                // Case 3: 叔叔是黑色，且当前节点是左孩子 (LL 型直线)
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
                // Case 2: 叔叔是黑色，且当前节点是左孩子 (RL 型折线)
                if (node == parent->left) {
                    rb_right_rotate(root, parent, augment_rotate);
                    node = parent;
                    parent = rb_parent(node);
                }
                // Case 3: 叔叔是黑色，且当前节点是右孩子 (RR 型直线)
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
 * @brief 将游离节点物理链入红黑树的指定位置
 */
static inline void rb_link_node(rb_node_t *node, rb_node_t *parent, rb_node_t **link) {
    node->parent_color = (uint64)parent; // 新节点默认颜色必定为 0 (红色，由于指针低位对齐)
    node->left = NULL;
    node->right = NULL;
    *link = node; // 将节点接入父节点的左/右指针中
}

/**
 * @brief 插入红黑树总控接口
 */
void rb_insert(rb_root_t *root, rb_node_t *node, rb_node_t *parent, rb_node_t **link, rb_augment_callbacks_f *augment_callbacks) {
    // 1. 物理挂载节点
    rb_link_node(node, parent, link);

    // 2. 🌟 增强数据向上传导 (若有回调则执行)
    if (augment_callbacks && augment_callbacks->propagate) {
        augment_callbacks->propagate(node, NULL);
    }

    // 3. 提取旋转回调，执行底层的红黑树颜色与结构调整
    augment_rotate_f rot = augment_callbacks ? augment_callbacks->rotate : NULL;
    rb_insert_fixup(root, node, rot);
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

        // --- 镜像分支 A：失衡节点位于左侧，兄弟在右侧 ---
        if (node == parent->left) {
            sibling = parent->right;

            // Case 1: 兄弟为红色。左旋父亲，使得新兄弟必定为黑色，降级为后续情况处理。
            if (rb_is_red(sibling)) {
                rb_set_black(sibling);
                rb_set_red(parent);
                rb_left_rotate(root, parent, augment_rotate);
                sibling = parent->right; // 更新兄弟节点
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
                    // 🌟 修复 Bug: 极端情况下防范空指针解引用
                    if (sibling->left) rb_set_black(sibling->left);
                    rb_set_red(sibling);
                    rb_right_rotate(root, sibling, augment_rotate);
                    sibling = parent->right; // 更新为新的真正的兄弟
                }
                // Case 4: 兄弟为黑，且右子为红 (RR 型)。一记左旋，彻底填补当前分支黑高！
                rb_set_color(sibling, rb_color(parent));
                rb_set_black(parent);
                if (sibling->right) rb_set_black(sibling->right); // 同样增加防范
                rb_left_rotate(root, parent, augment_rotate);
                break; // 填补完成，全局平衡恢复，跳出循环
            }
        }
        // --- 镜像分支 B：失衡节点位于右侧，兄弟在左侧 ---
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
                // Case 3: 兄弟为黑，左子为黑（必定右子为红）。LR 转换为 LL 型。
                if (!sibling->left || rb_is_black(sibling->left)) {
                    if (sibling->right) rb_set_black(sibling->right); // 🌟 防护
                    rb_set_red(sibling);
                    rb_left_rotate(root, sibling, augment_rotate);
                    sibling = parent->left;
                }
                // Case 4: 兄弟为黑，左子为红 (LL 型)。
                rb_set_color(sibling, rb_color(parent));
                rb_set_black(parent);
                if (sibling->left) rb_set_black(sibling->left); // 🌟 防护
                rb_right_rotate(root, parent, augment_rotate);
                break;
            }
        }
    }
    // 若原失衡节点其实是红色（或补偿过程中上移遇到了红色），直接染黑，完美补偿黑高
    if (node) rb_set_black(node);
}

/**
 * @brief 执行待删节点的物理剥离，若有双子节点则用后继节点替换
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

        // 🌟 触发回调：将旧节点的增强数据拷贝给替身节点
        if (augment_copy) {
            augment_copy(node, successor);
        }

        // 记录物理剔除点的信息（实际结构上消失的是 successor）
        parent = successor;
        child = successor->right;
        color = rb_color(successor);

        // 拔出 successor
        if (successor != node->right) {
            parent = rb_parent(successor);
            if (child) rb_set_parent(child, parent);

            parent->left = child;
            successor->right = node->right;
            rb_set_parent(successor->right, successor);
        }

        // successor 全盘顶替 node 的位置
        successor->left = node->left;
        rb_set_parent(node->left, successor);
        rb_set_parent_and_color(successor, rb_parent(node), rb_color(node));

        // 修复原父节点的指向
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

    // 导出物理移除点周围的信息，为平衡修正做准备
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

    // 安全提取回调函数
    augment_copy_f copy_cb = augment_callbacks ? augment_callbacks->copy : NULL;
    augment_rotate_f rot_cb = augment_callbacks ? augment_callbacks->rotate : NULL;

    // 1. 物理移除并用后继节点填补空缺
    rb_replace_erase(root, node, copy_cb, &parent, &child, &color);

    // 2. 🌟 物理剥离完毕后，立刻向上修正增强数据！
    // 此时的 parent 指向实际物理移除点（被删节点或其后继节点）的原始父亲。
    if (parent && augment_callbacks && augment_callbacks->propagate) {
        augment_callbacks->propagate(parent, NULL);
    }

    // 3. 若物理移除的是黑色节点，必定导致该分支黑高减 1，触发补偿修正
    if (color == rb_black) {
        rb_erase_fixup(root, child, parent, rot_cb);
    }
}