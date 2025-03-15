/*
 * 红黑树删除后重平衡核心逻辑
 * node:    当前需要调整的节点（可能为NULL）
 * parent:  node的父节点
 * root:    树的根节点
 */
static void __rb_erase_color(struct rb_node *node, struct rb_node *parent, struct rb_root *root)
{
    struct rb_node *sibling;

    // 循环处理，直到node是根节点或node变为红色
    while (node != root->rb_node && rb_is_black(node)) {
        if (node == parent->rb_left) { // 当前节点是父节点的左子节点
            sibling = parent->rb_right; // 兄弟节点为父节点的右子节点

            /* ------------- Case 1: 兄弟节点为红色 ------------- */
            if (rb_is_red(sibling)) {
                // 将兄弟节点染黑，父节点染红
                rb_set_black(sibling);
                rb_set_red(parent);
                // 对父节点进行左旋，使兄弟节点成为新的父节点
                __rb_rotate_left(parent, root);
                sibling = parent->rb_right; // 更新兄弟节点为原兄弟的左子节点
            }

            /* ---- Case 2: 兄弟节点为黑，且其子节点均为黑 ---- */
            if (rb_is_black(sibling->rb_left) && rb_is_black(sibling->rb_right)) {
                rb_set_red(sibling); // 兄弟节点染红
                node = parent;       // 问题上移至父节点
                parent = rb_parent(node); // 更新父指针
            } else {
                /* ---- Case 3: 兄弟节点为黑，且远侄子为红 ---- */
                if (rb_is_black(sibling->rb_right)) {
                    // 将兄弟节点的左子节点染黑，兄弟节点染红
                    rb_set_black(sibling->rb_left);
                    rb_set_red(sibling);
                    // 对兄弟节点进行右旋，转换为Case 4
                    __rb_rotate_right(sibling, root);
                    sibling = parent->rb_right; // 更新兄弟节点
                }

                // 此时兄弟的右子节点为红（Case 4）
                rb_set_color(sibling, rb_color(parent)); // 继承父节点颜色
                rb_set_black(parent);                   // 父节点染黑
                rb_set_black(sibling->rb_right);        // 兄弟右子染黑
                __rb_rotate_left(parent, root);         // 左旋父节点
                node = root->rb_node; // 调整完成，退出循环
                break;
            }
        } else { // 对称情况：当前节点是父节点的右子节点
            sibling = parent->rb_left;

            // Case 1镜像：兄弟节点为红
            if (rb_is_red(sibling)) {
                rb_set_black(sibling);
                rb_set_red(parent);
                __rb_rotate_right(parent, root);
                sibling = parent->rb_left;
            }

            // Case 2镜像：兄弟子节点均为黑
            if (rb_is_black(sibling->rb_left) && rb_is_black(sibling->rb_right)) {
                rb_set_red(sibling);
                node = parent;
                parent = rb_parent(node);
            } else {
                // Case 3镜像：近侄子为红
                if (rb_is_black(sibling->rb_left)) {
                    rb_set_black(sibling->rb_right);
                    rb_set_red(sibling);
                    __rb_rotate_left(sibling, root);
                    sibling = parent->rb_left;
                }

                // Case 4镜像：远侄子为红
                rb_set_color(sibling, rb_color(parent));
                rb_set_black(parent);
                rb_set_black(sibling->rb_left);
                __rb_rotate_right(parent, root);
                node = root->rb_node;
                break;
            }
        }
    }

    // 最终确保根节点为黑（性质2）
    if (node)
        rb_set_black(node);
}


/*
 * 红黑树删除主逻辑
 * 注意：被删除节点必须已存在于树中
 */
void rb_erase(struct rb_node *node, struct rb_root *root)
{
    struct rb_node *rebalance = NULL; // 重平衡起始节点
    struct rb_node *child;            // 被删除节点的子节点
    struct rb_node *parent;           // 被删除节点的父节点
    int color;                        // 被删除节点的原始颜色

    /* -------------------- 第1阶段：二叉搜索树删除 -------------------- */
    if (!node->rb_left) {             // Case 1: 左子节点为空
        child = node->rb_right;
    } else if (!node->rb_right) {     // Case 2: 右子节点为空
        child = node->rb_left;
    } else {                          // Case 3: 左右子节点均存在
        struct rb_node *old = node;   // 保存原始节点指针
        struct rb_node *left;         // 后继节点的左子节点

        node = node->rb_right;        // 移动到右子树
        while ((left = node->rb_left) != NULL)
            node = left;              // 找到右子树的最小节点（后继节点）

        child = node->rb_right;       // 后继节点的右子节点（可能为空）

        parent = rb_parent(node);     // 后继节点的父节点
        color = rb_color(node);       // 后继节点的颜色

        if (child)                    // 如果后继节点有右子节点
            rb_set_parent(child, parent); // 更新右子节点的父指针

        if (parent == old) {          // 后继节点是原始节点的直接右子节点
            parent->rb_right = child;
            parent = node;            // 调整parent指向后继节点
        } else {
            parent->rb_left = child;  // 将后继节点的右子树挂到父节点左子树
        }

        node->rb_left = old->rb_left; // 将原始节点的左子树转移给后继节点
        rb_set_parent(old->rb_left, node);

        node->rb_right = old->rb_right; // 处理右子树
        rb_set_parent(old->rb_right, node);

        node->rb_parent_color = old->rb_parent_color; // 继承颜色和父指针
        node->rb_left = old->rb_left;                 // 冗余赋值，确保正确性

        if (rb_parent(old)) {         // 更新原始节点父节点的子指针
            if (rb_parent(old)->rb_left == old)
                rb_parent(old)->rb_left = node;
            else
                rb_parent(old)->rb_right = node;
        } else {
            root->rb_node = node;     // 若删除的是根节点，更新根指针
        }

        rebalance = (color == RB_BLACK) ? child : NULL; // 仅当后继节点原为黑色时需要重平衡
        goto color_corrected;         // 跳转到颜色修正阶段
    }

    /* -------------------- 处理简单情况（0或1个子节点） ----------------- */
    parent = rb_parent(node);         // 获取父节点
    color = rb_color(node);           // 获取节点颜色

    if (child)                        // 如果存在子节点
        rb_set_parent(child, parent); // 更新子节点的父指针

    if (parent) {                     // 更新父节点的子指针
        if (parent->rb_left == node)
            parent->rb_left = child;
        else
            parent->rb_right = child;
    } else {
        root->rb_node = child;        // 若删除的是根节点，更新根指针
    }

    rebalance = (color == RB_BLACK) ? child : NULL; // 仅当删除黑色节点时需要重平衡

color_corrected:
    /* -------------------- 第2阶段：红黑树重平衡 -------------------- */
    if (rebalance)
        __rb_erase_color(rebalance, parent, root); // 调用内部平衡函数
}