#include "vmalloc.h"
#include "slub.h"
#include "printk.h"

typedef enum {
    rb_red = 0,
    rb_black = 1,
} rb_color_e;


typedef struct rb_node_t {
    UINT64 father_color; //父节点和颜色
    struct rb_node_t *left; //左子节点
    struct rb_node_t *right; //右子节点
} rb_node_t;

typedef struct rb_root_t {
    rb_node_t *rb_node; //树根
} rb_root_t;

/*------------ 基础操作函数 ------------*/
// 获取父节点（清除颜色位）
static inline rb_node_t *rb_father(const rb_node_t *node) {
    return (rb_node_t *) (node->father_color & ~3UL);
}

// 设置父节点（保留原有颜色）
static inline void rb_set_father(rb_node_t *node, rb_node_t *father) {
    node->father_color = (UINT64) father | (node->father_color & 3UL);
}

/*------------ 颜色操作函数 ------------*/
// 获取节点颜色（0为红，1为黑）
static inline UINT32 rb_color(const rb_node_t *node) {
    return node->father_color & 1;
}

// 判断是否为红色（颜色位为 0）
static inline BOOLEAN rb_is_red(const rb_node_t *node) {
    return !(node->father_color & 1);
}

// 判断是否为黑色（颜色位为 1）
static inline BOOLEAN rb_is_black(const rb_node_t *node) {
    return (node->father_color & 1);
}

// 设置为红色（清除颜色位后设为 0）
static inline void rb_set_red(rb_node_t *node) {
    node->father_color &= ~1UL; // ~1UL = 0xFFFF...FE，清除最低位
}

// 设置为黑色（保留父指针，设置颜色位为 1）
static inline void rb_set_black(rb_node_t *node) {
    node->father_color |= 1UL;
}

// 通用颜色设置函数（color 需为 RB_RED 或 RB_BLACK）
static inline void rb_set_color(rb_node_t *node, UINT32 color) {
    node->father_color = (node->father_color & ~1UL) | color;
}

/*------------ 高级组合操作 ------------*/
// 同时设置父节点和颜色（初始化或重链接时使用）
static inline void rb_set_father_and_color(rb_node_t *node, rb_node_t *father, UINT32 color) {
    node->father_color = (unsigned long) father | color;
}

//节点连接红黑树
static inline void rb_link_node(rb_node_t *node, rb_node_t *father, rb_node_t **link) {
    node->father_color = father;
    node->left = NULL;
    node->right = NULL;
    *link = node;
}


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
    while ((father = rb_father(node)) && rb_is_red(father)) {
        //当前节点为红色需修正
        gfather = rb_father(father);
        if (father == gfather->left) {
            //父为左则叔为右
            uncle = gfather->right;
            if (uncle && rb_is_red(uncle)) {
                //LXR型 叔叔为红色则把父叔变黑祖父变红，把当天节点设置为祖父继续修正
                rb_set_black(father);
                rb_set_black(uncle);
                rb_set_red(gfather);
                node = gfather;
            } else {
                //LXB型 叔叔为黑
                if (node == father->right) {
                    //LRB型 左旋父亲把形态调整为LLB型
                    rb_left_rotate(root, father);
                    node = father;
                    father = rb_father(node);
                }
                //LLB型 左旋祖父，父亲变黑，祖父便红
                rb_right_rotate(root, gfather);
                rb_set_black(father);
                rb_set_red(gfather);
            }
        } else {
            //父为右则叔为左（镜像）
            uncle = gfather->left;
            if (uncle && rb_is_red(uncle)) {
                //RXR型 叔叔为红色则把父叔变黑祖父变红，把当天节点设置为祖父继续修正
                rb_set_black(father);
                rb_set_black(uncle);
                rb_set_red(gfather);
                node = gfather;
            } else {
                //RXB型 叔叔为黑
                if (node == father->left) {
                    //RLB型 右旋父亲把形态调整为RRB型
                    rb_right_rotate(root, father);
                    node = father;
                    father = rb_father(node);
                }
                //RRB型 左旋祖父，父变黑，祖父变红
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
void rb_erase_color(rb_root_t *root, rb_node_t *node, rb_node_t *father) {
    rb_node_t *brother;
    // 循环处理，直到node是根节点或node变为红色
    while (node != root->rb_node && (node == NULL || rb_is_black(node))) {
        // 当前节点是父节点的右子节点，兄弟节点为父节点的左子节点
        if (node == father->right) {
            brother = father->left;
            //情况1: 兄弟节点为红色，兄变黑父变红，父亲右旋
            if (rb_is_red(brother)) {
                rb_right_rotate(root, father);
                rb_set_black(brother);
                rb_is_red(father);
                brother = father->left;
            }
            //兄弟节点为黑
            if (rb_is_black(brother)) {
                if ((brother->left && rb_is_red(brother->left)) || (brother->right && rb_is_red(brother->right))) {
                    //情况2： 兄弟黑色孩子是红色  ,LRR型：左兄的右孩是红色，左旋兄弟转为LLR型
                    if (!brother->left || rb_is_black(brother->left)) {
                        rb_left_rotate(root, brother);
                        brother = father->left;
                    }
                    //LLR型：左兄的左孩是红色，右旋父亲，兄弟继承父亲颜色，父亲和左孩变黑
                    rb_right_rotate(root, father);
                    rb_set_color(brother, rb_color(father));
                    rb_set_black(father);
                    rb_set_black(brother->left);
                    break;
                }
                //情况3：兄弟黑的两个孩子也是黑色，兄弟染红，双黑上移
                rb_set_red(brother);
                node = father;
                father = rb_father(father);
            }
        } else {
            // 当前节点是父节点的左子节点，兄弟节点为父节点的右子节点
            brother = father->right;
            //情况1: 兄弟节点为红色，兄变黑父变红，父亲左旋
            if (rb_is_red(brother)) {
                rb_left_rotate(root, father);
                rb_set_black(brother);
                rb_set_red(father);
                brother = father->right;
            }
            //兄弟节点为黑
            if (rb_is_black(brother)) {
                if ((brother->left && rb_is_red(brother->left)) || (brother->right && rb_is_red(brother->right))) {
                    //情况2： 兄弟节的孩子是红色  RLR型：右兄的左孩是红色，右旋兄弟
                    if (brother->right == NULL && rb_is_red(brother->left)) {
                        rb_right_rotate(root, brother);
                        brother = father->left;
                    }
                    //RRR型：右兄的右孩是红色，左旋父亲，兄弟继承父亲颜色，父亲和左孩变黑
                    rb_left_rotate(root, father);
                    rb_set_color(brother, rb_color(father));
                    rb_set_black(father);
                    rb_set_black(brother->right);
                    break;
                }
                //情况3：兄弟黑色两个孩子也是黑色, 兄弟染红，双黑上移
                rb_set_red(brother);
                node = father;
                father = rb_father(father);
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
    UINT64 keyare[34] = {83, 22, 99, 35, 95, 78, 75, 92, 40, 76, 93, 41};
    rb_root_t *root = kmalloc(sizeof(rb_root_t));
    root->rb_node = NULL;
    my_data_t *new_data;

    for (int i = 0; i < 12; i++) {
        new_data = kmalloc(sizeof(my_data_t));
        new_data->key = keyare[i];
        insert_my_data(root, new_data);
    }

    mid_traversal(root, root->rb_node);
    color_printk(GREEN,BLACK, "\n");

    rb_node_t *node;
    UINT64 keyare1[34] = {78, 35, 95, 22, 83, 75, 92, 40, 76, 93, 41, 99};
    for (int i = 0; i < 12; i++) {
        node = rb_find(root, keyare1[i]);
        rb_erase(root, node);
        mid_traversal(root, root->rb_node);
        color_printk(GREEN,BLACK, "                        \n");
    }
}
