#include "vmalloc.h"
#include "slub.h"
#include "printk.h"
#include "rbtree.h"



typedef struct my_data_t {
    rb_node_t rb_node;
    UINT64 key;
} my_data_t;


UINT32 insert_my_data(rb_root_t *root, my_data_t *new_data) {
    rb_node_t **link = &root->rb_node;
    rb_node_t *father = NULL;
    my_data_t *entry;

    while (*link) {
        father = *link;
        entry = (my_data_t *) father;
        if (new_data->key < entry->key) {
            link = &father->left;
        } else if (new_data->key > entry->key) {
            link = &father->right;
        } else {
            return 1;
        }
    }

    rb_link_node(&new_data->rb_node, father, link);
    rb_insert_color(root, &new_data->rb_node);
}

/* 传入key查找node */
rb_node_t *rb_find(rb_root_t *root, UINT64 key) {
    rb_node_t *node = root->rb_node;
    while (node) {
        if (((my_data_t *) node)->key == key) return node; //搜索key对应的节点
        node = key < ((my_data_t *) node)->key ? node->left : node->right;
    }
    return NULL; //没有找到
}


/*//中序遍历
void mid_traversal1(rb_root_t *rbtree) {
    rb_node_t *cur_node = rbtree->root;
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
            while (cur_node->father != NULL && cur_node->father->right == cur_node) {
                cur_node = cur_node->father;
            }
            cur_node = cur_node->father;
        }
    }
}*/


//递归中序遍历
void mid_traversal(rb_root_t *root, rb_node_t *node) {
    if (!node) return;
    mid_traversal(root, node->left); //处理左子树
    color_printk(GREEN, BLACK, "key:%d   color:%d\n", ((my_data_t *) node)->key, rb_color(node));
    mid_traversal(root, node->right); //处理右子树
}


void rb_test(void) {
    //红黑树测试
    UINT64 keyare[34] = {10,9,8,7,6,5,4,3,2,1};
    rb_root_t *root = kmalloc(sizeof(rb_root_t));
    root->rb_node = NULL;
    my_data_t *new_data;

    for (int i = 0; i < 10; i++) {
        new_data = kmalloc(sizeof(my_data_t));
        new_data->key = keyare[i];
        insert_my_data(root, new_data);
    }

    mid_traversal(root, root->rb_node);
    color_printk(GREEN,BLACK, "\n");

    rb_node_t *node;
    UINT64 keyare1[34] = {4};
    for (int i = 0; i < 6; i++) {
        node = rb_find(root, keyare1[i]);
        rb_erase(root, node);
        mid_traversal(root, root->rb_node);
        color_printk(GREEN,BLACK, "                        \n");
    }
}
