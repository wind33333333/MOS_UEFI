#include "vmalloc.h"
#include "slub.h"

typedef enum {
    RED,        //红色0
    BLACK       //黑色1
} color_e;

typedef struct rbtree_node_t {
    struct rbtree_node_t *left;     //左子节点
    struct rbtree_node_t *right;    //右子节点
    struct rbtree_node_t *parent;   //父节点
    color_e color;                  //颜色
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
    new_node->color = RED;
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
    }else if (new_root->parent->left=root){
        new_root->parent->left = new_root;        //父亲左孩变成新根
    }else if (new_root->parent->right=root) {
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
    }else if (new_root->parent->left=root){
        new_root->parent->left = new_root;        //父亲左孩变成新根
    }else if (new_root->parent->right=root) {
        new_root->parent->right = new_root;       //父亲右孩变成新根
    }

}

//修正红黑树插入失衡情况
void rbtree_insert_fixup(rbtree_t *rbtree, rbtree_node_t *cur) {
    rbtree_node_t *uncle;
    while (cur->parent->color == RED) {
        if (cur->parent == cur->parent->parent->left) {//LXX
            uncle = cur->parent->parent->right;
            if (uncle->color == RED) {     //LXR
                cur->parent->color = BLACK;
                uncle->color = BLACK;
                cur->parent->parent->color = RED;
                cur = cur->parent->parent;
            } else if (uncle->color == BLACK) { //LXB
                if (cur == cur->parent->right) { //LRB
                    cur = cur->parent;
                    left_rotate(rbtree, cur);
                }
                cur->parent->color = BLACK;     //LLB
                cur->parent->parent->color = RED;
                right_rotate(rbtree, cur->parent->parent);
            }
        }else if (cur->parent == cur->parent->parent->right) {//RXX
            uncle = cur->parent->parent->left;
            if (uncle->color == RED) {     //RXR
                cur->parent->color = BLACK;
                uncle->color = BLACK;
                cur->parent->parent->color = RED;
                cur = cur->parent->parent;
            } else if (uncle->color == BLACK) { //RXB
                if (cur == cur->parent->left) { //RLB
                    cur = cur->parent;
                    right_rotate(rbtree, cur);
                }
                cur->parent->color = BLACK;     //RRB
                cur->parent->parent->color = RED;
                left_rotate(rbtree, cur->parent->parent);
            }
        }
    }
    rbtree->root->color = BLACK;
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



}



