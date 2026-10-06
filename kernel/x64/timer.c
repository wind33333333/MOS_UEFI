#include "timer.h"
#include "../time/time.h"
#include "task_sched.h"

void sleep_us(uint64 delay_us) {
    uint64 flags;
    local_irq_save(&flags); // 关中断保护

    task_t *curr = g_rq.current;

    // 1. 记账：算好个人的醒来时间，并把自己的状态改为“睡觉”
    uint64 wake_ns = get_uptime_ns() + (delay_us * 1000ULL);
    curr->wake_up_ns = wake_ns;
    curr->state = TASK_SLEEPING;

    // 2. 存入账本：把当前任务挂入红黑树
    // (这里的 rb_insert 是你优化过的那套 O(1) NULL 判断的安全代码)
    rb_node_t **link = &g_rq.sleep_tree.rb_node; // 指向当前需要比较的节点指针
    rb_node_t *parent = NULL;                    // 记录未来新节点的父亲

    // 从根节点开始向下遍历，直到遇到空指针（即找到了叶子挂载点）
    while (*link) {
        parent = *link;
        // 通过树节点指针，反向获取到它所属的 task_t 结构体
        // 假设你定义了 rb_entry，类似于 Linux 的 rb_entry 宏
        task_t *parent_task = CONTAINER_OF(parent, task_t, sleep_node);

        // 比较唤醒时间 (Key)
        if (curr->wake_up_ns < parent_task->wake_up_ns) {
            link = &parent->left;  // 比父节点小，去左子树找
        } else {
            link = &parent->right; // 大于或等于父节点，去右子树找
        }
    }

    rb_insert(&g_rq.sleep_tree, &curr->sleep_node, parent, link, NULL);

    // 3. 🌟 对接你的漂亮硬件抽象！
    // 检查账本：我刚刚加进去的这个任务，是不是全系统最早要醒的？
    if (rb_first(&g_rq.sleep_tree) == &curr->sleep_node) {
        // 如果是，马上更新 CPU 的全局记录，并通知底层硬件改闹钟！
        reprogram_clockevent(wake_ns);
    }

    // 4. 强制交出麦克风！
    // 不管别人死活，我自己要睡了，调用 schedule 切给下一个人！
    schedule();

    // --- (漫长的等待...) ---
    // 5. 等未来某一天时钟中断触发，Timer ISR 把你从红黑树里摘下来，
    // schedule() 才会返回，代码才会走到这里！
    local_irq_restore(flags);
}


/**
 * @brief 扫描睡眠树，唤醒所有到期的任务，并重设下一个硬件闹钟
 */
#define EARLY_WAKEUP_TOLERANCE_NS 2000ULL // 容差窗口：2微秒
#define MAX_TIME 0xFFFFFFFFFFFFFFFFULL

void check_and_wakeup_sleeping_tasks(void) {
    uint64 now_ns = get_uptime_ns();

    // ========================================================
    // 🔪 第一把镰刀：EEVDF 虚拟时间片剥夺 (保持上一版的修正)
    // ========================================================
    task_t *curr = g_rq.current;
    if (curr && curr != g_rq.idle_task) {
        update_curr();
        if (curr->v_eligible >= curr->v_deadline) {
            curr->flags |= TIF_NEED_RESCHED;
        }
    }

    // ========================================================
    // 捞人逻辑 (保持不变)
    // ========================================================
    uint64 effective_now = now_ns + EARLY_WAKEUP_TOLERANCE_NS;
    rb_node_t *node;
    while ((node = rb_first(&g_rq.sleep_tree)) != NULL) {
        task_t *sleep_task = CONTAINER_OF(node, task_t, sleep_node);
        if (sleep_task->wake_up_ns > effective_now) break;

        rb_erase(&g_rq.sleep_tree, &sleep_task->sleep_node, NULL);
        sleep_task->state = TASK_READY;
        enqueue_task_eevdf(sleep_task);

        if (sleep_task != g_rq.current) {
            g_rq.current->flags |= TIF_NEED_RESCHED;
        }
    }

    // =======================================================
    // 🌟 终极魔法 3.0：双轨接力 (睡眠死线 vs 调度死线)
    // =======================================================
    uint64 sleep_deadline = MAX_TIME;
    uint64 sched_deadline = MAX_TIME;

    // 1. 获取最近的睡眠死线 (Clock A)
    node = rb_first(&g_rq.sleep_tree);
    if (node != NULL) {
        task_t *next_sleep_task = CONTAINER_OF(node, task_t, sleep_node);
        sleep_deadline = next_sleep_task->wake_up_ns;
    }

    // 2. 🌟 获取调度死线 (Clock B)
    // 核心判定：只有当就绪队列里有其他人排队时，我们才需要设定剥夺闹钟！
    if (curr && curr != g_rq.idle_task && g_rq.sched_tree.rb_node != NULL) {
        // 如果上面第一把镰刀没给它贴标签，说明它还有剩余虚拟时间
        if (!(curr->flags & TIF_NEED_RESCHED) && (curr->v_deadline > curr->v_eligible)) {
            // 反向物理折算：剩余虚拟时间 -> 剩余物理时间
            uint64 v_left = curr->v_deadline - curr->v_eligible;
            uint64 phys_left = (v_left * curr->weight) / NICE_0_LOAD;

            sched_deadline = now_ns + phys_left;
        } else {
            // 如果已经被贴了标签，说明现在立马就该切走，时间就是现在
            sched_deadline = now_ns;
        }
    }

    // 3. 终极裁决：谁离现在最近，硬件闹钟就听谁的！
    uint64 final_deadline = (sleep_deadline < sched_deadline) ? sleep_deadline : sched_deadline;

    reprogram_clockevent(final_deadline);
}



