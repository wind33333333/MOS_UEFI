#include "vmalloc.h"
#include "slub.h"
#include "printk.h"

typedef enum {
    red_node, //红色0
    black_node //黑色1
} rbtree_color_e;

typedef struct rbtree_node_t {
    struct rbtree_node_t *left; //左子节点
    struct rbtree_node_t *right; //右子节点
    struct rbtree_node_t *parent; //父节点
    rbtree_color_e color; //颜色
    UINT64 key; //数据
} rbtree_node_t;

typedef struct rbtree_t {
    rbtree_node_t *root; //树根
    //rbtree_node_t *nil; //哨兵节点
} rbtree_t;

//创建新红黑树节点
rbtree_node_t *create_rbtree_node(rbtree_t *rbtree, UINT64 key) {
    rbtree_node_t *new_node = (rbtree_node_t *) kmalloc(sizeof(rbtree_node_t));
    new_node->left = NULL;
    new_node->right = NULL;
    new_node->parent = NULL;
    new_node->color = red_node;
    new_node->key = key;
    return new_node;
}

//左旋
void left_rotate(rbtree_t *rbtree, rbtree_node_t *root) {
    rbtree_node_t *new_root = root->right; //原根的右孩变新根
    rbtree_node_t *left_child = new_root->left;
    root->right = left_child; //新根的左孩变原根的右孩
    new_root->left = root; //原根变新根左孩
    new_root->parent = root->parent; //原根的父亲变新根父亲
    root->parent = new_root; //新根变成原根的父亲

    if (left_child) {
        left_child->parent = root; //原根变左孩的父亲
    }

    if (!new_root->parent) {
        rbtree->root = new_root; //新根变成树根
    } else if (new_root->parent->left == root) {
        new_root->parent->left = new_root; //父亲左孩变成新根
    } else if (new_root->parent->right == root) {
        new_root->parent->right = new_root; //父亲右孩变成新根
    }
}

//右旋
void right_rotate(rbtree_t *rbtree, rbtree_node_t *root) {
    rbtree_node_t *new_root = root->left; //原根的左孩变新根
    rbtree_node_t *right_child = new_root->right;
    root->left = right_child; //新根的右孩变原根的左孩
    new_root->right = root; //原根变新根右孩
    new_root->parent = root->parent; //原根的父亲变新根父亲
    root->parent = new_root; //新根变成原根的父亲

    if (right_child) {
        right_child->parent = root; //原根变右孩的父亲
    }

    if (!new_root->parent) {
        rbtree->root = new_root; //新根变成树根
    } else if (new_root->parent->left == root) {
        new_root->parent->left = new_root; //父亲左孩变成新根
    } else if (new_root->parent->right == root) {
        new_root->parent->right = new_root; //父亲右孩变成新根
    }
}

//修正红黑树插入失衡情况
void rb_insert_color(rbtree_t *rbtree, rbtree_node_t *cur) {
    rbtree_node_t *uncle;
    while (cur->parent && cur->parent->color == red_node) {
        if (cur->parent == cur->parent->parent->left) {
            //LXX
            uncle = cur->parent->parent->right;
            if (uncle && uncle->color == red_node) {
                //LXR
                cur->parent->color = black_node;
                uncle->color = black_node;
                cur->parent->parent->color = red_node;
                cur = cur->parent->parent;
            } else if (!uncle || uncle->color == black_node) {
                //LXB
                if (cur == cur->parent->right) {
                    //LRB
                    cur = cur->parent;
                    left_rotate(rbtree, cur);
                }
                cur->parent->color = black_node; //LLB
                cur->parent->parent->color = red_node;
                right_rotate(rbtree, cur->parent->parent);
            }
        } else if (cur->parent == cur->parent->parent->right) {
            //RXX
            uncle = cur->parent->parent->left;
            if (uncle && uncle->color == red_node) {
                //RXR
                cur->parent->color = black_node;
                uncle->color = black_node;
                cur->parent->parent->color = red_node;
                cur = cur->parent->parent;
            } else if (!uncle || uncle->color == black_node) {
                //RXB
                if (cur == cur->parent->left) {
                    //RLB
                    cur = cur->parent;
                    right_rotate(rbtree, cur);
                }
                cur->parent->color = black_node; //RRB
                cur->parent->parent->color = red_node;
                left_rotate(rbtree, cur->parent->parent);
            }
        }
    }
    rbtree->root->color = black_node;
}

//插入节点到红黑树
void rbtree_insert(rbtree_t *rbtree, rbtree_node_t *insert_node) {
    if (insert_node == NULL) return;

    rbtree_node_t *cur_node = rbtree->root;
    rbtree_node_t *prev_node = NULL;

    //查找红黑树大小合适的节点
    while (cur_node) {
        prev_node = cur_node;
        if (insert_node->key < cur_node->key) {
            cur_node = cur_node->left;
        } else if (insert_node->key > cur_node->key) {
            cur_node = cur_node->right;
        } else {
            return;
        }
    }

    insert_node->parent = prev_node;
    // prev_node等于nil时说明当前是树根直接把node插入根，否则根据key大小插入左右孩子
    if (!prev_node) {
        rbtree->root = insert_node;
    } else if (insert_node->key < prev_node->key) {
        prev_node->left = insert_node;
    } else if (insert_node->key > prev_node->key) {
        prev_node->right = insert_node;
    }

    rb_insert_color(rbtree, insert_node);
}


//中序遍历
void mid_traversal1(rbtree_t *rbtree) {
    rbtree_node_t *cur_node = rbtree->root;
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
}

/*
 * 红黑树删除后重平衡核心逻辑
 * node:    当前需要调整的节点（可能为NULL）
 * parent:  node的父节点
 * root:    树的根节点
 */
static void rb_erase_color(rbtree_t *rbtree, rbtree_node_t *node, rbtree_node_t *parent) {
    rbtree_node_t *sibling;

    // 循环处理，直到node是根节点或node变为红色
    while (node != rbtree->root && (node == NULL || node->color == black_node)) {
        // 当前节点是父节点的右子节点，兄弟节点为父节点的左子节点
        if (node == parent->right) {
            sibling = parent->left;
            /* ---- Case 1: 兄弟节点为红色 ----- */
            if (sibling->color == red_node) {
                //兄变黑父变红，父亲右旋
                sibling->color = black_node;
                parent->color = red_node;
                right_rotate(rbtree, parent);
                sibling = parent->left;
            }

            /* ---- Case 2: 兄弟节点为黑---- */
            if (sibling->color == black_node) {
                //case2.1 兄弟的两个孩子也是黑色
                if (sibling->left == NULL && sibling->right == NULL) {
                    //兄弟染红，双黑上移
                    sibling->color = red_node;
                    node = parent;
                    parent = parent->parent;

                }else{//case2.2 兄弟节的孩子是红色
                    //左兄的右孩是红色 LRR型
                    if (sibling->left == NULL && sibling->right->color == red_node) {
                        left_rotate(rbtree, sibling); //左旋兄弟
                        sibling = parent->left;
                    }
                    //左兄的左孩是红色  LLR型
                    sibling->color = parent->color; //兄弟继承父亲颜色，父亲和左孩变黑
                    parent->color = black_node;
                    sibling->left->color = black_node;
                    right_rotate(rbtree, parent); //右旋父亲
                    break;
                }
            }
        }else {// 当前节点是父节点的左子节点，兄弟节点为父节点的右子节点
            sibling = parent->right;
            /* ----- Case 1: 兄弟节点为红色 ----- */
            if (sibling->color == red_node) {
                //兄变黑父变红，父亲左旋
                sibling->color = black_node;
                parent->color = red_node;
                left_rotate(rbtree, parent);
                sibling = parent->right;
            }

            /* ---- Case 2: 兄弟节点为黑---- */
            if (sibling->color == black_node) {
                //case2.1 兄弟的两个孩子也是黑色
                if (sibling->left == NULL && sibling->right == NULL) {
                    //兄弟染红，双黑上移
                    sibling->color = red_node;
                    node = parent;
                    parent = parent->parent;

                }else{//case2.2 兄弟节的孩子是红色
                    //右兄的左孩是红色 RLR型
                    if (sibling->right == NULL && sibling->left->color == red_node) {
                        right_rotate(rbtree, sibling); //右旋兄弟
                        sibling = parent->left;
                    }
                    //右兄的右孩是红色  RRR型
                    sibling->color = parent->color; //兄弟继承父亲颜色，父亲和左孩变黑
                    parent->color = black_node;
                    sibling->right->color = black_node;
                    left_rotate(rbtree, parent); //右旋父亲
                    break;
                }
            }
        }
    }

    // 最终确保根节点为黑
    if (node) node->color = black_node;
}

/*
 * 红黑树删除主逻辑
 * 注意：被删除节点必须已存在于树中
 */
void rbtree_delete(rbtree_t *rbtree, UINT64 key) {
    //通过key查找node
    rbtree_node_t *del_node = rbtree->root;
    while (del_node) {
        if (del_node->key == key) break; //搜索key对应的节点
        del_node = key < del_node->key ? del_node->left : del_node->right;
    }
    if (!del_node) return; //没有找到

    /**********************************************/

    rbtree_node_t *parent; // 父亲节点
    rbtree_node_t *child; // 被删除节点的孩子节点
    int color; // 被删除节点的原始颜色

    if (del_node->left && del_node->right) {
        //情况1：删除节点左右子树都有，找后继节点
        rbtree_node_t *successor = del_node->right;
        while (successor->left) successor = successor->left; //找后继

        parent = successor;
        child = successor->right; // 后继节点的右子节点（可能为空）
        color = successor->color; // 后继节点的颜色

        if (successor != del_node->right) {
            // 后继节点不是原始节点的直接右子节点
            parent = successor->parent;
            if (child) child->parent = parent; // 如果后继节点有右子节点,更新右子节点的父指针
            parent->left = child; // 后继节点父亲的左子节点，更新为后继节点的右子节点
            successor->right = del_node->right; // 后继节点的右子节点，更新为原始节点右孩
            del_node->right->parent = successor; // 原色节点的右子节点父指针更新为后继节点
        }
        successor->left = del_node->left; // 后继节点的左子节点，更新为原始节点的左孩
        del_node->left->parent = successor; // 原始节点左子节点的父亲，更新为后继节点
        successor->parent = del_node->parent; // 后继节点的父节点，更新为原始节点的父节点
        successor->color = del_node->color; // 后继节点继承原始节点颜色

        if (del_node->parent) {
            if (del_node == del_node->parent->left) {
                del_node->parent->left = successor; // 原始节点为父节点左孩，更新为后继节点
            } else {
                del_node->parent->right = successor; // 原始节点为父节点右孩，更新为后继节点
            }
        } else {
            rbtree->root = successor; // 更新根节点
        }
    } else {
        //情况2：只有1个子树或0个子树
        child = del_node->left ? del_node->left : del_node->right;
        parent = del_node->parent; //被删除节点的父亲
        color = del_node->color; //被删除节点颜色

        if (child) child->parent = parent; //有孩子则更新孩子的父亲指针为删除节点的父亲
        if (parent) {
            //被删除节点有父亲
            if (del_node == parent->left) {
                //被删除节点为父亲的左孩
                parent->left = child; //把孩子更新为父亲的左孩
            } else {
                parent->right = child; //把孩子更新为父亲的右孩
            }
        } else {
            //被删除节点无父亲则是根节点
            rbtree->root = child; //把孩子更新为新的根节点
        }
    }

    if (color == black_node) rb_erase_color(rbtree, child, parent);

}

//递归中序遍历
void mid_traversal(rbtree_t *rbtree, rbtree_node_t *node) {
    if (!node) return;
    mid_traversal(rbtree, node->left); //处理左子树
    color_printk(GREEN, black_node, "key:%d   color:%d\n", node->key, node->color);
    mid_traversal(rbtree, node->right); //处理右子树
}


void rb_test(void) {
    //红黑树测试
    UINT64 keyare[34] = {83, 22, 99, 35, 95, 78, 75, 92, 40, 76, 93, 41};
    rbtree_t *rbtree = kmalloc(sizeof(rbtree_t));
    rbtree->root = NULL;
    rbtree_node_t *node = NULL;

    for (int i = 0; i < 11; i++) {
        node = create_rbtree_node(rbtree, keyare[i]);
        rbtree_insert(rbtree, node);
    }

    mid_traversal1(rbtree);
    rbtree_delete(rbtree, 78);
}
