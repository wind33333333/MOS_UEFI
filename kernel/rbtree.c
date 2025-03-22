#include "rbtree.h"

//左旋
void rb_left_rotate(rb_root_t *root, rb_node_t *node) {
    //获取右子节点（旋转后的新父节点)
    rb_node_t *new_father = node->right;
    //右子节点的左子节点挂到旋转节点的右子节点
    node->right = new_father->left;
    //更新右子节点的父指针（如果存在）
    if (node->right) rb_set_father(node->right, node);
    //旋转节点挂到右子节点的左子节点
    new_father->left = node;
    //右子节点的父亲更新为旋转节点的父亲
    rb_set_father(new_father, rb_father(node));
    //旋转节点的父亲更新为右子节点
    rb_set_father(node, new_father);

    if (!rb_father(new_father)) {
        //如果父节点是空则当天节点是根节点，更新根指针
        root->rb_node = new_father;
    } else if (node == rb_father(new_father)->left) {
        //更新父节点的左指针
        rb_father(new_father)->left = new_father;
    } else if (node == rb_father(new_father)->right) {
        //更新父节点的右指针
        rb_father(new_father)->right = new_father;
    }
}

/**右旋把旋转节点的左子变成新的父亲节点，旋转节点变成新父节点的左子**/
void rb_right_rotate(rb_root_t *root, rb_node_t *node) {
    //旋转节点的左子变新父节点
    rb_node_t *new_father = node->left;
    //新父的右子变选转点的左子
    node->left = new_father->right;
    //更新左子节点的父指针
    if (node->left) rb_set_father(node->left, node);
    //旋转节点变新父的右子
    new_father->right = node;
    //更新新父节点的父指针
    rb_set_father(new_father, rb_father(node));
    //更新旋转节点的父指针
    rb_set_father(node, new_father);

    if (!rb_father(new_father)) {
        //更新根节点
        root->rb_node = new_father;
    } else if (node == rb_father(new_father)->left) {
        //更新新父节点的左子指针
        rb_father(new_father)->left = new_father;
    } else if (node == rb_father(new_father)->right) {
        //更新新父节点的右子指针
        rb_father(new_father)->right = new_father;
    }
}

//修正红黑树插入失衡情况
void rb_insert_color(rb_root_t *root, rb_node_t *node) {
    rb_node_t *uncle, *father, *gfather;
    //当前节点为红色需修正
    while ((father = rb_father(node)) && rb_is_red(father)) {
        gfather = rb_father(father);
        if (father == gfather->left) {
            //父为左则叔为右
            uncle = gfather->right;
            if (uncle && rb_is_red(uncle)) {
                //情况1：LXR型 叔叔为红色，则把父叔变黑，祖父变红，把当前节点设置为祖父继续修正
                rb_set_black(father);
                rb_set_black(uncle);
                rb_set_red(gfather);
                node = gfather;
            } else {
                //LXB型 叔叔为黑
                if (node == father->right) {
                    //情况2：LXB型->LLB 左旋父亲把形态调整
                    rb_left_rotate(root, father);
                    node = father;
                    father = rb_father(node);
                }
                //情况3：LLB型 左旋祖父，父亲变黑，祖父变红
                rb_right_rotate(root, gfather);
                rb_set_black(father);
                rb_set_red(gfather);
            }
        } else {
            //父为右则叔为左（镜像）
            uncle = gfather->left;
            if (uncle && rb_is_red(uncle)) {
                //情况1：RXR型 叔叔为红色，则把父叔变黑，祖父变红，把当前节点设置为祖父继续修正
                rb_set_black(father);
                rb_set_black(uncle);
                rb_set_red(gfather);
                node = gfather;
            } else {
                //RXB型 叔叔为黑
                if (node == father->left) {
                    //情况2：RLB型->RRB型 右旋父亲把形态调整为RRB型
                    rb_right_rotate(root, father);
                    node = father;
                    father = rb_father(node);
                }
                //情况3：RRB型 左旋祖父，父变黑，祖父变红
                rb_left_rotate(root, gfather);
                rb_set_black(father);
                rb_set_red(gfather);
            }
        }
    }
    //保持根节点黑色
    rb_set_black(root->rb_node);
}

/*
 * 红黑树删除后重平衡核心逻辑
 * node:    当前需要调整的节点（可能为NULL）
 * father:  node的父节点
 * root:    树的根节点
 */
static inline void rb_erase_color(rb_root_t *root, rb_node_t *node, rb_node_t *father) {
    rb_node_t *brother;
    // 循环处理，直到node是根节点或node变为红色
    while (node != root->rb_node && (!node || rb_is_black(node))) {
        // 当前节点是父节点的右子节点，兄弟节点为父节点的左子节点
        if (node == father->right) {
            brother = father->left;
            //情况1: 兄弟节点为红色，父亲右旋,兄变黑,父变红
            if (rb_is_red(brother)) {
                rb_right_rotate(root, father);
                rb_set_black(brother);
                rb_set_red(father);
                brother = father->left;
            }
            //兄弟节点为黑
            if (rb_is_black(brother)) {
                //左孩黑
                if (!brother->left || rb_is_black(brother->left)) {
                    //情况2：兄弟和左右孩子都是黑色, 兄弟染红，双黑上移，继续修正
                    if (!brother->right || rb_is_black(brother->right)) {
                        rb_set_red(brother);
                        node = father;
                        father = rb_father(father);
                        continue;
                    }
                    //情况3：LRR型->LLR型：兄弟的左孩是黑色，右孩是红色，左旋兄弟
                    rb_left_rotate(root, brother);
                    brother = father->left;
                }
                //情况4：LLR型：兄弟的左孩是红色，右旋父亲，兄弟继承父亲颜色，父亲和左孩变黑，黑高修复完成
                rb_right_rotate(root, father);
                rb_set_color(brother, rb_color(father));
                rb_set_black(father);
                rb_set_black(brother->left);
                break;
            }
        } else {
            // 当前节点是父节点的左子节点，兄弟节点为父节点的右子节点
            brother = father->right;
            //情况1: 兄弟节点为红色，父亲左旋,兄变黑,父变红
            if (rb_is_red(brother)) {
                rb_left_rotate(root, father);
                rb_set_black(brother);
                rb_set_red(father);
                brother = father->right;
            }
            //兄弟节点为黑
            if (rb_is_black(brother)) {
                //右孩黑
                if (!brother->right || rb_is_black(brother->right)) {
                    //情况2：兄弟和左右孩子都是黑色, 兄弟染红，双黑上移，继续修正
                    if (!brother->left || rb_is_black(brother->left)) {
                        rb_set_red(brother);
                        node = father;
                        father = rb_father(father);
                        continue;
                    }
                    //情况3：RLR型->RRR型：兄弟的右孩是黑色，左孩是红色，右旋兄弟
                    rb_right_rotate(root, brother);
                    brother = father->left;
                }
                //情况4：RRR型：兄弟的右孩是红色，左旋父亲，兄弟继承父亲颜色，父亲和左孩变黑，黑高修复完成
                rb_left_rotate(root, father);
                rb_set_color(brother, rb_color(father));
                rb_set_black(father);
                rb_set_black(brother->right);
                break;
            }
        }
    }
    // 最终确保根节点为黑
    if (node) rb_set_black(node);
}


/*
 * 红黑树删除主逻辑
 * 注意：被删除节点必须已存在于树中
 */
void rb_erase(rb_root_t *root, rb_node_t *node) {
    rb_node_t *father;
    rb_node_t *child;
    rb_color_e color;

    if (node->left && node->right) {
        //情况1：删除节点左右子树都有，找后继节点
        rb_node_t *successor = node->right;
        while (successor->left) successor = successor->left;

        father = successor;
        // 后继节点的右子节点（可能为空）
        child = successor->right;
        // 后继节点的颜色
        color = rb_color(successor);

        // 后继节点不是原始节点的直接右子节点
        if (successor != node->right) {
            father = rb_father(successor);
            // 如果后继节点有右子节点,更新右子节点的父指针
            if (child) rb_set_father(child, father);
            // 后继节点父亲的左子节点，更新为后继节点的右子节点
            father->left = child;
            // 后继节点的右子节点，更新为原始节点右孩
            successor->right = node->right;
            // 右子节点父指针更新为后继节点
            rb_set_father(successor->right, successor);
        }
        // 后继节点的左子节点，更新为原始节点的左孩
        successor->left = node->left;
        // 原始节点左子节点的父亲，更新为后继节点
        rb_set_father(node->left, successor);
        // 后继节点的父节点，更新为原始节点的父节点,后继节点继承原始节点颜色
        rb_set_father_and_color(successor, rb_father(node), rb_color(node));

        if (rb_father(node)) {
            // 原始节点为父节点左孩，更新为后继节点
            if (node == rb_father(node)->left) {
                rb_father(node)->left = successor;
            } else {
                // 原始节点为父节点右孩，更新为后继节点
                rb_father(node)->right = successor;
            }
        } else {
            // 更新根节点
            root->rb_node = successor;
        }
    } else {
        //情况2：只有1个子树或0个子树
        child = node->left ? node->left : node->right;
        //被删除节点的父亲
        father = rb_father(node);
        //被删除节点颜色
        color = rb_color(node);
        if (child) rb_set_father(child, father); //有孩子则更新孩子的父亲指针为删除节点的父亲
        //被删除节点有父亲
        if (father) {
            //被删除节点为父亲的左孩,把孩子更新为父亲的左孩
            if (node == father->left) {
                father->left = child;
            } else {
                //被删除节点为父亲的左孩,把孩子更新为父亲的右孩
                father->right = child;
            }
        } else {
            //被删除节点无父亲则是根节点,把孩子更新为新的根节点
            root->rb_node = child;
        }
    }

    if (color == rb_black) rb_erase_color(root, child, father);
}