#include "vmalloc.h"
#include "slub.h"
#include "printk.h"

typedef enum {
    red_node,        //红色0
    black_node       //黑色1
} rbtree_color_e;

typedef struct rbtree_node_t {
    struct rbtree_node_t *left;     //左子节点
    struct rbtree_node_t *right;    //右子节点
    struct rbtree_node_t *parent;   //父节点
    rbtree_color_e color;           //颜色
    UINT64 key;                     //数据
}rbtree_node_t;

typedef struct rbtree_t{
    rbtree_node_t *root;            //树根
    rbtree_node_t *nil;             //哨兵节点
}rbtree_t;

//创建新红黑树节点
rbtree_node_t *create_rbtree_node(rbtree_t *rbtree, UINT64 key) {
    rbtree_node_t *new_node = (rbtree_node_t *)kmalloc(sizeof(rbtree_node_t));
    new_node->left = rbtree->nil;
    new_node->right = rbtree->nil;
    new_node->parent = rbtree->nil;
    new_node->color = red_node;
    new_node->key = key;
    return new_node;
}

//左旋
void left_rotate(rbtree_t *rbtree, rbtree_node_t *root) {
    rbtree_node_t *new_root = root->right;        //原根的右孩变新根
    rbtree_node_t *left_child = new_root->left;
    root->right = left_child;                     //新根的左孩变原根的右孩
    new_root->left = root;                        //原根变新根左孩
    new_root->parent = root->parent;              //原根的父亲变新根父亲
    root->parent = new_root;                      //新根变成原根的父亲

    if (left_child != rbtree->nil) {
        left_child->parent = root;                //原根变左孩的父亲
    }

    if (new_root->parent == rbtree->nil) {
        rbtree->root = new_root;                  //新根变成树根
    }else if (new_root->parent->left==root){
        new_root->parent->left = new_root;        //父亲左孩变成新根
    }else if (new_root->parent->right==root) {
        new_root->parent->right = new_root;       //父亲右孩变成新根
    }

}

//右旋
void right_rotate(rbtree_t *rbtree, rbtree_node_t *root) {
    rbtree_node_t *new_root = root->left;        //原根的左孩变新根
    rbtree_node_t *right_child = new_root->right;
    root->left = right_child;                     //新根的右孩变原根的左孩
    new_root->right = root;                        //原根变新根右孩
    new_root->parent = root->parent;              //原根的父亲变新根父亲
    root->parent = new_root;                      //新根变成原根的父亲

    if (right_child != rbtree->nil) {
        right_child->parent = root;                //原根变右孩的父亲
    }

    if (new_root->parent == rbtree->nil) {
        rbtree->root = new_root;                  //新根变成树根
    }else if (new_root->parent->left==root){
        new_root->parent->left = new_root;        //父亲左孩变成新根
    }else if (new_root->parent->right==root) {
        new_root->parent->right = new_root;       //父亲右孩变成新根
    }

}

//修正红黑树插入失衡情况
void rbtree_insert_fixup(rbtree_t *rbtree, rbtree_node_t *cur) {
    rbtree_node_t *uncle;
    while (cur->parent->color == red_node) {
        if (cur->parent == cur->parent->parent->left) {//LXX
            uncle = cur->parent->parent->right;
            if (uncle->color == red_node) {     //LXR
                cur->parent->color = black_node;
                uncle->color = black_node;
                cur->parent->parent->color = red_node;
                cur = cur->parent->parent;
            } else if (uncle->color == black_node) { //LXB
                if (cur == cur->parent->right) { //LRB
                    cur = cur->parent;
                    left_rotate(rbtree, cur);
                }
                cur->parent->color = black_node;     //LLB
                cur->parent->parent->color = red_node;
                right_rotate(rbtree, cur->parent->parent);
            }
        }else if (cur->parent == cur->parent->parent->right) {//RXX
            uncle = cur->parent->parent->left;
            if (uncle->color == red_node) {     //RXR
                cur->parent->color = black_node;
                uncle->color = black_node;
                cur->parent->parent->color = red_node;
                cur = cur->parent->parent;
            } else if (uncle->color == black_node) { //RXB
                if (cur == cur->parent->left) { //RLB
                    cur = cur->parent;
                    right_rotate(rbtree, cur);
                }
                cur->parent->color = black_node;     //RRB
                cur->parent->parent->color = red_node;
                left_rotate(rbtree, cur->parent->parent);
            }
        }
    }
    rbtree->root->color = black_node;
}

//插入节点到红黑树
void rbtree_insert(rbtree_t *rbtree, rbtree_node_t *insert_node) {
    rbtree_node_t *cur_node=rbtree->root;
    rbtree_node_t *prev_node=rbtree->nil;

    //查找红黑树大小合适的节点
    while (cur_node != rbtree->nil) {
        prev_node = cur_node;
        if (insert_node->key < cur_node->key) {
            cur_node = cur_node->left;
        }else if (insert_node->key > cur_node->key) {
            cur_node = cur_node->right;
        }else {
            return;
        }
    }

    insert_node->parent = prev_node;
    // prev_node等于nil时说明当前是树根直接把node插入根，否则根据key大小插入左右孩子
    if (prev_node == rbtree->nil) {
        rbtree->root = insert_node;
    }else if (insert_node->key < prev_node->key) {
        prev_node->left = insert_node;
    }else if (insert_node->key > prev_node->key) {
        prev_node->right = insert_node;
    }

    rbtree_insert_fixup(rbtree, insert_node);
}

//删除红黑树节点
void rbtree_delete(rbtree_t *rbtree, UINT64 key) {
    rbtree_node_t *cur_node=rbtree->root;
    while (cur_node->key != key) {   //搜索key对应的节点
        if (cur_node == rbtree->nil) return;  //没有找到
        if (cur_node->key < key) {
            cur_node = cur_node->left;
        }else if (cur_node->key > key) {
            cur_node = cur_node->right;
        }
    }

    //被删除的节点只有左孩或者只有右孩（这种被删除节点为黑色，孩子节点为红色，不然会违反黑路同或不红红）
    if (cur_node->left != rbtree->nil && cur_node->right == rbtree->nil) {
        //只有左孩
        if (cur_node == cur_node->parent->left) {
            cur_node->parent->left = cur_node->left;
        }else if (cur_node == cur_node->parent->right) {
            cur_node->parent->right = cur_node->left;
        }
        cur_node->left->parent = cur_node->parent;
        cur_node->left->color = BLACK;
        kfree(cur_node);
        return;
    }else if (cur_node->right != rbtree->nil && cur_node->left == rbtree->nil) {
        //只有右孩
        if (cur_node == cur_node->parent->left) {
            cur_node->parent->left = cur_node->right;
        }else if (cur_node == cur_node->parent->right) {
            cur_node->parent->right = cur_node->right;
        }
        cur_node->right->parent = cur_node->parent;
        cur_node->right->color = BLACK;
        kfree(cur_node);
        return;
    }


}

//递归中序遍历
void mid_traversal(rbtree_t *rbtree, rbtree_node_t *node) {
    if (node == rbtree->nil) return;
    mid_traversal(rbtree, node->left);  //处理左子树
    color_printk(GREEN,black_node,"key:%d   color:%d\n",node->key,node->color);
    mid_traversal(rbtree, node->right); //处理右子树
    
}

//中序遍历
void mid_traversal1(rbtree_t *rbtree) {
    rbtree_node_t *cur_node=rbtree->root;
    while (cur_node->left != rbtree->nil) {     //找到最左边的节点
        cur_node = cur_node->left;
    }

    while (cur_node != rbtree->nil) {
        color_printk(GREEN,BLACK,"key:%d   color:%d\n",cur_node->key,cur_node->color);
        if (cur_node->right != rbtree->nil) {   // 有右子树，转向右子树的最左节点
            cur_node = cur_node->right;
            while (cur_node->left != rbtree->nil) {
                cur_node = cur_node->left;
            }
        }else {// 没有右子树，回溯到父节点
            while (cur_node->parent != NULL && cur_node->parent->right == cur_node) {
                cur_node = cur_node->parent;
            }
            cur_node = cur_node->parent;
        }
    }

}

void rb_test(void) {
    //红黑树测试
    UINT64 keyare [34] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34};
    rbtree_t *rbtree = kmalloc(sizeof(rbtree_t));
    rbtree->nil = kmalloc(sizeof(rbtree_node_t));
    rbtree->nil->left = rbtree->nil;
    rbtree->nil->right = rbtree->nil;
    rbtree->nil->parent = rbtree->nil;
    rbtree->nil->color = black_node;
    rbtree->nil->key = 0;
    rbtree->root = rbtree->nil;
    rbtree_node_t *node = rbtree->nil;

    for (int i = 0; i < 34; i++) {
        node = create_rbtree_node(rbtree,keyare[i]);
        rbtree_insert(rbtree,node);
    }

    mid_traversal1(rbtree);
}

