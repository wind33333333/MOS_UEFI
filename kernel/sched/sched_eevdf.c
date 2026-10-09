#include "../sched/sched_eevdf.h"
#include "../sched/sched.h"

/**
 * @brief 核心计算：重新计算当前节点的子树最小 Vd
 */
static void eevdf_update_min_vd(task_t *t) {
    t->min_v_deadline = t->v_deadline; // 初始假设自己最小

    // 探测左子树
    if (t->sched_node.left) {
        task_t *l = CONTAINER_OF(t->sched_node.left, task_t, sched_node);
        if (l->min_v_deadline < t->min_v_deadline) {
            t->min_v_deadline = l->min_v_deadline;
        }
    }
    // 探测右子树
    if (t->sched_node.right) {
        task_t *r = CONTAINER_OF(t->sched_node.right, task_t, sched_node);
        if (r->min_v_deadline < t->min_v_deadline) {
            t->min_v_deadline = r->min_v_deadline;
        }
    }
}

/**
 * @brief [增强回调] 向上游传导更新 min_vd
 */
static void eevdf_augment_propagate(rb_node_t *node, rb_node_t *stop) {
    while (node != stop) {
        task_t *t = CONTAINER_OF(node, task_t, sched_node);
        uint64 old_min = t->min_v_deadline;

        eevdf_update_min_vd(t);

        // 🌟 极致优化：如果本节点的最小 Vd 没发生变化，上面的祖先绝对不会变，直接熔断！
        if (t->min_v_deadline == old_min) {
            break;
        }
        node = rb_parent(node);
    }
}

/**
 * @brief [增强回调] 节点被物理替换时，全盘继承数据
 */
static void eevdf_augment_copy(rb_node_t *old_node, rb_node_t *new_node) {
    task_t *old_t = CONTAINER_OF(old_node, task_t, sched_node);
    task_t *new_t = CONTAINER_OF(new_node, task_t, sched_node);
    new_t->min_v_deadline = old_t->min_v_deadline;
}

/**
 * @brief [增强回调] 红黑树旋转时，重新计算新老父节点的增强数据
 */
static void eevdf_augment_rotate(rb_node_t *old_node, rb_node_t *new_node) {
    task_t *old_t = CONTAINER_OF(old_node, task_t, sched_node);
    task_t *new_t = CONTAINER_OF(new_node, task_t, sched_node);

    // 必须先算被降级的旧节点，再算被提拔的新节点 (自底向上)
    eevdf_update_min_vd(old_t);
    eevdf_update_min_vd(new_t);
}

// 封装为回调结构体供 rb_insert 和 rb_erase 调用
rb_augment_callbacks_f eevdf_callbacks = {
    .propagate = eevdf_augment_propagate,
    .copy      = eevdf_augment_copy,
    .rotate    = eevdf_augment_rotate
};



/**
 * @brief 将任务按 EEVDF 规则推入就绪树
 */
void enqueue_task_eevdf(task_t *task) {
    // 🌟 核心防线：idle_task 是兜底幽灵，绝对不能进排队名单！
    if (task == g_rq.idle_task) {
        task->state = TASK_READY;
        return; // 直接放行，不挂树！
    }

    // 1. 计算虚拟截止时间: Vd = Ve + (slice / weight)
    uint64 v_slice = (uint64)(((__uint128_t)task->time_slice * task->w_mult) >> WMULT_SHIFT);
    task->v_deadline = task->v_eligible + v_slice;

    // 初始增强数据
    task->min_v_deadline = task->v_deadline;

    // 2. 红黑树常规 O(log N) 挂载查找 (以 v_eligible 为 Key)
    rb_node_t **link = &g_rq.sched_tree.rb_node;
    rb_node_t *parent = NULL;

    while (*link) {
        parent = *link;
        task_t *parent_task = CONTAINER_OF(parent, task_t, sched_node);

        if (task->v_eligible < parent_task->v_eligible) {
            link = &parent->left;
        } else {
            link = &parent->right; // 相同 Ve 当作大于处理 (FIFO)
        }
    }

    // 3. 执行增强红黑树插入
    rb_insert(&g_rq.sched_tree, &task->sched_node, parent, link, &eevdf_callbacks);
    task->state = TASK_READY;
}

/**
 * @brief 从就绪树中拔出任务
 */
void dequeue_task_eevdf(task_t *task) {
    rb_erase(&g_rq.sched_tree, &task->sched_node, &eevdf_callbacks);
}


/**
 * @brief 🌟 EEVDF 核心裁决器：挑选下一个最适合运行的任务
 * @details
 * 核心数学法则：在所有“合格”的任务（v_eligible <= 系统 vtime）中，
 * 挑选“虚拟截止时间”（v_deadline）最小的任务。
 */
task_t* pick_next_task_eevdf(void) {
    // 从 EEVDF 增强红黑树的根节点开始向下扫描
    rb_node_t *node = g_rq.sched_tree.rb_node;
    task_t *best = NULL;

    while (node) {
        task_t *curr = CONTAINER_OF(node, task_t, sched_node);
        task_t *left = curr->sched_node.left ? CONTAINER_OF(curr->sched_node.left, task_t, sched_node) : NULL;

        // ==========================================================
        // 1. 合格性防线：当前节点是否有资格运行？
        // ==========================================================
        // 如果 curr->v_eligible > vtime，说明它“太超前了”，不合格。
        // 根据红黑树性质，右子树的 Ve 必定比当前节点更大，肯定也全都不合格。
        // 所以此时唯一可能藏着合格任务的地方，只有左子树！
        if (curr->v_eligible > g_rq.vtime) {
            node = curr->sched_node.left;
            continue;
        }

        // ==========================================================
        // 2. 走到这里，说明 curr 已经合格 (Ve <= Vtime)。
        // 极其重要的定理：因为左子树的所有节点的 Ve 都 <= curr 的 Ve，
        // 所以此时此刻，【整个左子树必定全员合格】！
        // ==========================================================

        // 记录或更新最佳候选人 (best)
        // 判定条件：要么还没选出 best；要么 curr 的截止时间更紧急 (Vd更小)
        // Tie-breaker 平局决胜：如果 Vd 完全一样，挑选等得更久的 (Ve更小)
        if (!best || curr->v_deadline < best->v_deadline ||
           (curr->v_deadline == best->v_deadline && curr->v_eligible < best->v_eligible)) {
            best = curr;
        }

        // ==========================================================
        // 3. 🌟 终极防饿死路由法则 (Linux 落袋为安策略)
        // ==========================================================
        // 既然左子树已经【全员合格】了，只要左子树里藏着的最小值 (min_v_deadline)
        // 能够打败我们刚刚确立的 best，我们就【绝对不能放过它】，必须去左边把它挖出来！
        if (left && left->min_v_deadline < best->v_deadline) {
            node = curr->sched_node.left;
        } else {
            // 否则，说明左子树里全是“辣鸡”，没有比当前 best 更好的人选了。
            // 此时，真正的最小值要么是 best 自己，要么藏在未知的【右子树】里。
            // 毫不犹豫地向右走，去碰碰运气（即使右子树的任务可能不合格，留给下一次循环判定）。
            node = curr->sched_node.right;
        }
    }

    // ==========================================================
    // 4. 极端边缘保护：时间线快进 (Fast-forward)
    // ==========================================================
    // 如果系统因为所有任务都在睡眠，导致全局 Vtime 严重滞后，
    // 此时树上可能一个合格的任务都找不到 (best == NULL)。
    // 补救措施：直接去红黑树最左端（Ve 最小，等得最久的任务），
    // 强行把系统的全局时间拨快，对齐到它的 Ve，让它强制合格！
    if (!best) {
        rb_node_t *leftmost = rb_first(&g_rq.sched_tree);
        if (leftmost) {
            best = CONTAINER_OF(leftmost, task_t, sched_node);
            g_rq.vtime = best->v_eligible;
        }
    }

    return best;
}

/**
 * @brief 计费函数：推进当前任务的虚拟时间
 */
void update_cur_task_eevdf(uint64 cur_ns) {
    task_t *cur_task = g_rq.cur_task;
    if (!cur_task || cur_task == g_rq.idle_task) return;

    // 🌟 修复 Bug 5：时间回退与下溢安全检查
    if (cur_ns <= cur_task->last_update_time) {
        cur_task->last_update_time = cur_ns;
        return;
    }

    uint64 delta_exec = cur_ns- cur_task->last_update_time;
    cur_task->last_update_time = cur_ns;

    // 计算消耗的虚拟时间: 物理时间 * (基准权重 / 自身权重)
    // 权重越大的任务，虚拟时间流逝越慢
    uint64 delta_vruntime = (uint64)(((__uint128_t)delta_exec * cur_task->w_mult) >> WMULT_SHIFT);

    // 因为是运行中，它的合格时间(Ve)随之推进
    cur_task->v_eligible += delta_vruntime;

    // 系统全局虚拟时间平滑追随当前任务
    if (cur_task->v_eligible > g_rq.vtime) {
        g_rq.vtime = cur_task->v_eligible;
    }
}