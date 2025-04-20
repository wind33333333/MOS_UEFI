#include "rbtree.h"

/*
 * 左旋把node的右子节点变成新的父节点，node点变成新父节点的左子节点
 * root:根节点
 * node:旋转点
 * augment_rotate:增强旋转回调函数
 */
static void rb_left_rotate(rb_root_t *root, rb_node_t *node, augment_rotate_f augment_rotate) {
    //获取右子节点（旋转后的新父节点)
    rb_node_t *new_parent = node->right;
    //右子节点的左子节点挂到旋转节点的右子节点
    node->right = new_parent->left;
    //更新右子节点的父指针（如果存在）
    if (node->right) rb_set_parent(node->right, node);
    //旋转节点挂到右子节点的左子节点
    new_parent->left = node;
    //右子节点的父亲更新为旋转节点的父亲
    rb_set_parent(new_parent, rb_parent(node));
    //旋转节点的父亲更新为右子节点
    rb_set_parent(node, new_parent);

    if (!rb_parent(new_parent)) {
        //如果父节点是空则当前节点是根节点，更新根指针
        root->rb_node = new_parent;
    } else if (node == rb_parent(new_parent)->left) {
        //更新父节点的左指针
        rb_parent(new_parent)->left = new_parent;
    } else if (node == rb_parent(new_parent)->right) {
        //更新父节点的右指针
        rb_parent(new_parent)->right = new_parent;
    }

    //用户自定义回调函数处理旋转
    augment_rotate(node, new_parent);
}

/*
 * 右旋把node的左子变成新的父亲节点，node变成新父节点的右子节点
 * root:根节点
 * node:旋转点
 * augment_rotate:增强旋转回调函数
 */
static void rb_right_rotate(rb_root_t *root, rb_node_t *node, augment_rotate_f augment_rotate) {
    //旋转节点的左子变新父节点
    rb_node_t *new_parent = node->left;
    //新父的右子变选转点的左子
    node->left = new_parent->right;
    //更新左子节点的父指针
    if (node->left) rb_set_parent(node->left, node);
    //旋转节点变新父的右子
    new_parent->right = node;
    //更新新父节点的父指针
    rb_set_parent(new_parent, rb_parent(node));
    //更新旋转节点的父指针
    rb_set_parent(node, new_parent);

    if (!rb_parent(new_parent)) {
        //更新根节点
        root->rb_node = new_parent;
    } else if (node == rb_parent(new_parent)->left) {
        //更新新父节点的左子指针
        rb_parent(new_parent)->left = new_parent;
    } else if (node == rb_parent(new_parent)->right) {
        //更新新父节点的右子指针
        rb_parent(new_parent)->right = new_parent;
    }

    //用户自定义回调函数处理旋转
    augment_rotate(node, new_parent);
}

/*
 * 修复红黑树插入失衡
 * root:根节点
 * node:当前需要调整的节点（可能为NULL）
 * augment_callbacks:增强回调函数集合
 */
void rb_insert_fixup(rb_root_t *root, rb_node_t *node, rb_augment_callbacks_f *augment_callbacks) {
    rb_node_t *uncle, *parent, *gparent;
    //当前节点为红色需修正
    while ((parent = rb_parent(node)) && rb_is_red(parent)) {
        gparent = rb_parent(parent);
        if (parent == gparent->left) {
            //父为左则叔为右
            uncle = gparent->right;
            if (uncle && rb_is_red(uncle)) {
                //情况1：LXR型 叔叔为红色，则把父叔变黑，祖父变红，把当前节点设置为祖父继续修正
                rb_set_black(parent);
                rb_set_black(uncle);
                rb_set_red(gparent);
                node = gparent;
            } else {
                //LXB型 叔叔为黑
                if (node == parent->right) {
                    //情况2：LXB型->LLB 左旋父亲把形态调整
                    rb_left_rotate(root, parent, augment_callbacks->rotate);
                    node = parent;
                    parent = rb_parent(node);
                }
                //情况3：LLB型 左旋祖父，父亲变黑，祖父变红
                rb_right_rotate(root, gparent, augment_callbacks->rotate);
                rb_set_black(parent);
                rb_set_red(gparent);
            }
        } else {
            //父为右则叔为左（镜像）
            uncle = gparent->left;
            if (uncle && rb_is_red(uncle)) {
                //情况1：RXR型 叔叔为红色，则把父叔变黑，祖父变红，把当前节点设置为祖父继续修正
                rb_set_black(parent);
                rb_set_black(uncle);
                rb_set_red(gparent);
                node = gparent;
            } else {
                //RXB型 叔叔为黑
                if (node == parent->left) {
                    //情况2：RLB型->RRB型 右旋父亲把形态调整为RRB型
                    rb_right_rotate(root, parent, augment_callbacks->rotate);
                    node = parent;
                    parent = rb_parent(node);
                }
                //情况3：RRB型 左旋祖父，父变黑，祖父变红
                rb_left_rotate(root, gparent, augment_callbacks->rotate);
                rb_set_black(parent);
                rb_set_red(gparent);
            }
        }
    }
    //保持根节点黑色
    rb_set_black(root->rb_node);
    //用户自定义回调函数处理向上修复
    augment_callbacks->propagate(node,NULL);
}

/*
 * 修复红黑树删除失衡
 * root:树的根节点
 * node:当前需要调整的节点（可能为NULL）
 * parent:node的父节点
 * augment_callbacks:增强回调函数集合
 */
static inline void rb_erase_fixup(rb_root_t *root, rb_node_t *node, rb_node_t *parent,
                                  rb_augment_callbacks_f *augment_callbacks) {
    rb_node_t *sibling;
    // 循环处理，直到node是根节点或node变为红色
    while (node != root->rb_node && (!node || rb_is_black(node))) {
        // 当前节点是父节点的右子节点，兄弟节点为父节点的左子节点
        if (node == parent->right) {
            sibling = parent->left;
            //情况1: 兄弟节点为红色，父亲右旋,兄变黑,父变红
            if (rb_is_red(sibling)) {
                rb_right_rotate(root, parent, augment_callbacks->rotate);
                rb_set_black(sibling);
                rb_set_red(parent);
                sibling = parent->left;
            }
            //兄弟节点为黑
            if (rb_is_black(sibling)) {
                //左孩黑
                if (!sibling->left || rb_is_black(sibling->left)) {
                    //情况2：兄弟和左右孩子都是黑色, 兄弟染红，双黑上移，继续修正
                    if (!sibling->right || rb_is_black(sibling->right)) {
                        rb_set_red(sibling);
                        node = parent;
                        parent = rb_parent(parent);
                        continue;
                    }
                    //情况3：LRR型->LLR型：兄弟的左孩是黑色，右孩是红色，左旋兄弟
                    rb_left_rotate(root, sibling, augment_callbacks->rotate);
                    sibling = parent->left;
                }
                //情况4：LLR型：兄弟的左孩是红色，右旋父亲，兄弟继承父亲颜色，父亲和左孩变黑，黑高修复完成
                rb_right_rotate(root, parent, augment_callbacks->rotate);
                rb_set_color(sibling, rb_color(parent));
                rb_set_black(parent);
                rb_set_black(sibling->left);
                break;
            }
        } else {
            // 当前节点是父节点的左子节点，兄弟节点为父节点的右子节点
            sibling = parent->right;
            //情况1: 兄弟节点为红色，父亲左旋,兄变黑,父变红
            if (rb_is_red(sibling)) {
                rb_left_rotate(root, parent, augment_callbacks->rotate);
                rb_set_black(sibling);
                rb_set_red(parent);
                sibling = parent->right;
            }
            //兄弟节点为黑
            if (rb_is_black(sibling)) {
                //右孩黑
                if (!sibling->right || rb_is_black(sibling->right)) {
                    //情况2：兄弟和左右孩子都是黑色, 兄弟染红，双黑上移，继续修正
                    if (!sibling->left || rb_is_black(sibling->left)) {
                        rb_set_red(sibling);
                        node = parent;
                        parent = rb_parent(parent);
                        continue;
                    }
                    //情况3：RLR型->RRR型：兄弟的右孩是黑色，左孩是红色，右旋兄弟
                    rb_right_rotate(root, sibling, augment_callbacks->rotate);
                    sibling = parent->left;
                }
                //情况4：RRR型：兄弟的右孩是红色，左旋父亲，兄弟继承父亲颜色，父亲和左孩变黑，黑高修复完成
                rb_left_rotate(root, parent, augment_callbacks->rotate);
                rb_set_color(sibling, rb_color(parent));
                rb_set_black(parent);
                rb_set_black(sibling->right);
                break;
            }
        }
    }
    // 最终确保根节点为黑
    if (node) rb_set_black(node);
    //用户自定义回调函数处理向上修复
    augment_callbacks->propagate(node,NULL);
}


/*
 * 红黑树删除
 * root:根节点
 * node:需要删除的节点
 * augment_callbacks:增强回调函数
 */
void rb_erase(rb_root_t *root, rb_node_t *node, rb_augment_callbacks_f *augment_callbacks) {
    rb_node_t *parent, *child;
    rb_color_e color;

    if (node->left && node->right) {
        //情况1：删除节点左右子树都有，找后继节点
        rb_node_t *successor = node->right;
        while (successor->left) successor = successor->left;

        //用户自定义回调函数处理后继节点
        augment_callbacks->copy(node, successor);

        parent = successor;
        // 后继节点的右子节点（可能为空）
        child = successor->right;
        // 后继节点的颜色
        color = rb_color(successor);

        // 后继节点不是原始节点的直接右子节点
        if (successor != node->right) {
            parent = rb_parent(successor);
            // 如果后继节点有右子节点,更新右子节点的父指针
            if (child) rb_set_parent(child, parent);
            // 后继节点父亲的左子节点，更新为后继节点的右子节点
            parent->left = child;
            // 后继节点的右子节点，更新为原始节点右孩
            successor->right = node->right;
            // 右子节点父指针更新为后继节点
            rb_set_parent(successor->right, successor);
        }
        // 后继节点的左子节点，更新为原始节点的左孩
        successor->left = node->left;
        // 原始节点左子节点的父亲，更新为后继节点
        rb_set_parent(node->left, successor);
        // 后继节点的父节点，更新为原始节点的父节点,后继节点继承原始节点颜色
        rb_set_parent_and_color(successor, rb_parent(node), rb_color(node));

        if (rb_parent(node)) {
            // 原始节点为父节点左孩，更新为后继节点
            if (node == rb_parent(node)->left) {
                rb_parent(node)->left = successor;
            } else {
                // 原始节点为父节点右孩，更新为后继节点
                rb_parent(node)->right = successor;
            }
        } else {
            // 更新根节点
            root->rb_node = successor;
        }
    } else {
        //情况2：只有1个子树或0个子树
        child = node->left ? node->left : node->right;
        //被删除节点的父亲
        parent = rb_parent(node);
        //被删除节点颜色
        color = rb_color(node);
        if (child) rb_set_parent(child, parent); //有孩子则更新孩子的父亲指针为删除节点的父亲
        //被删除节点有父亲
        if (parent) {
            //被删除节点为父亲的左孩,把孩子更新为父亲的左孩
            if (node == parent->left) {
                parent->left = child;
            } else {
                //被删除节点为父亲的左孩,把孩子更新为父亲的右孩
                parent->right = child;
            }
        } else {
            //被删除节点无父亲则是根节点,把孩子更新为新的根节点
            root->rb_node = child;
        }
    }

    if (color == rb_black) rb_erase_fixup(root, child, parent, augment_callbacks);
}

/*
 * 空的加强旋转函数
 */
static void empty_augment_rotate(rb_node_t *old_node, rb_node_t *new_node) {
}

/*
 * 空的加强复制函数
 */
static void empty_augment_copy(rb_node_t *old_node, rb_node_t *new_node) {
}

/*
 * 空的加强向上修复函数
 */
static void empty_augment_propagate(rb_node_t *start_node, rb_node_t *stop_node) {
}

/*
 * 全局红黑树加强操作空函数集合
 */
rb_augment_callbacks_f empty_augment_callbacks;

/*
 * 全局红黑树加强操作空函数集合初始化
 */
void INIT_TEXT init_rbtree(void) {
    empty_augment_callbacks.rotate = empty_augment_rotate;
    empty_augment_callbacks.copy = empty_augment_copy;
    empty_augment_callbacks.propagate = empty_augment_propagate;
}
