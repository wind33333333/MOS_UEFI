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



