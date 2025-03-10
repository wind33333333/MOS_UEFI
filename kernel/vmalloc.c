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

//查找后继节点
rbtree_node_t *find_successor(rbtree_t *rbtree, rbtree_node_t *root) {
    rbtree_node_t *cur_node = root->right;
    if (!cur_node) return cur_node;
    while (cur_node->left) {
        cur_node = cur_node->left;
    }
    return cur_node;
}

//交换节点位置
void swap_node(rbtree_t *rbtree, rbtree_node_t *a, rbtree_node_t *b) {
    rbtree_node_t b_back = *b;

    //b节点替换a节点
    if (a == a->parent->left) {
        //a为父节点左孩
        a->parent->left = b;
    } else if (a == a->parent->right) {
        //a为父节点右孩
        a->parent->right = b;
    } else if (a == rbtree->root) {
        //a为树根
        rbtree->root = b;
    }

    b->left = b == a->left ? a : a->left;
    b->right = b == a->right ? a : a->right;
    if (a->parent != b) b->parent = a->parent;
    b->color = a->color;

    if (b->left) b->left->parent = b;
    if (b->right) b->right->parent = b;

    //a节点替换b节点
    if (b == b_back.parent->left) {
        b_back.parent->left = a;
    } else if (b == b_back.parent->right) {
        b_back.parent->right = a;
    } else if (b == rbtree->root) {
        rbtree->root = a;
    }

    a->left = a == b_back.left ? b : b_back.left;
    a->right = a == b_back.right ? b : b_back.right;
    if (b_back.parent != a) a->parent = b_back.parent;
    a->color = b_back.color;

    if (a->left) a->left->parent = a;
    if (a->right) a->right->parent = a;
}

//删除节点
void delete_node(rbtree_t *rbtree, rbtree_node_t *node) {
    if (node == node->parent->left) {
        node->parent->left = node->left;
    } else if (node == node->parent->right) {
    }
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

//删除红黑树节点
void rbtree_delete(rbtree_t *rbtree, UINT64 key) {
    //通过key查找node
    rbtree_node_t *cur_node = rbtree->root;
    while (cur_node->key != key) {
        //搜索key对应的节点
        if (!cur_node) return; //没有找到
        cur_node = key < cur_node->key ? cur_node->left : cur_node->right;
    }

    //情况1：删除节点左右子树都有，把要删除的节点和后继节点位置交换颜色不换
    if (cur_node->left && cur_node->right) {
        rbtree_node_t *successor = find_successor(rbtree, cur_node);
        swap_node(rbtree, cur_node, successor);
    }


    //情况2：删除节点只有一个子树，必定父黑子红。
    if (cur_node->left || cur_node->right) {
        delete_node(rbtree, cur_node);
    }

    mid_traversal1(rbtree);

    if (cur_node->left) {
        swap_node(rbtree, cur_node, cur_node->left);
    } else if (cur_node->right) {
        swap_node(rbtree, cur_node, cur_node->right);
    }

    //情况3：删除节点没有子树且是黑色节点


    mid_traversal1(rbtree);


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
    UINT64 keyare[34] = {83, 22, 99, 35, 95, 78, 75, 92, 40, 76, 93};
    rbtree_t *rbtree = kmalloc(sizeof(rbtree_t));
    rbtree->root = NULL;
    rbtree_node_t *node = NULL;

    for (int i = 0; i < 11; i++) {
        node = create_rbtree_node(rbtree, keyare[i]);
        rbtree_insert(rbtree, node);
    }

    mid_traversal1(rbtree);
    rbtree_delete(rbtree, 83);
}
