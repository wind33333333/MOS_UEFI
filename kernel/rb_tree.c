
//情况1: 兄弟节点为红色，父亲右旋,兄变黑,父变红
if (rb_is_red(brother)) {
    rb_right_rotate(root, father);
    rb_set_black(brother);
    rb_is_red(father);
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