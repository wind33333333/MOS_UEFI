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
void check_and_wakeup_sleeping_tasks(void) {
    // 1. 获取挂钟时间
    uint64 now_ns = get_uptime_ns();

    // 🌟 将容差直接加在当前时间上，形成“有效当前时间”
    // 在这个时间线之前的所有任务，一律统统叫醒！
    uint64 effective_now = now_ns + EARLY_WAKEUP_TOLERANCE_NS;

    rb_node_t *node;

    // 2. 循环检查睡眠红黑树
    while ((node = rb_first(&g_rq.sleep_tree)) != NULL) {
        task_t *sleep_task = CONTAINER_OF(node, task_t, sleep_node);

        // 如果连最左侧（最早）的人，唤醒时间都大于 effective_now
        // 说明所有人都还没睡够，直接停止捞人！
        if (sleep_task->wake_up_ns > effective_now) {
            break;
        }

        // =======================================================
        // 时间到了，捞人！
        // =======================================================
        rb_erase(&g_rq.sleep_tree, &sleep_task->sleep_node, NULL);
        sleep_task->state = TASK_READY;
        enqueue_task_eevdf(sleep_task);

        if (sleep_task != g_rq.current) {
            g_rq.current->flags |= TIF_NEED_RESCHED; // 贴上换人标签
        }
    }

    // =======================================================
    // 3. 终极魔法：Tickless 续杯 / APIC 接力 (Relay)
    // =======================================================
    // 刚才的循环可能捞出了几个人，也可能一个都没捞出来（比如 APIC 中途溢出早醒）。
    // 不管怎样，我们再看一眼树上还有没有人在睡：
    node = rb_first(&g_rq.sleep_tree);
    if (node != NULL) {
        task_t *next_sleep_task = CONTAINER_OF(node, task_t, sleep_node);
        reprogram_clockevent(next_sleep_task->wake_up_ns);
    } else {
        // 所有人都醒了，把硬件闹钟关掉，或者设成最大值
        reprogram_clockevent(0xFFFFFFFFFFFFFFFFULL);
    }
}




