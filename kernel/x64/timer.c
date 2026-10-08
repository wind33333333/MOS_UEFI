#include "timer.h"

#include "interrupt.h"
#include "../time/time_core.h"
#include "task_sched.h"

void sleep_us(uint64 delay_us) {
    uint64 flags;
    local_irq_save(&flags); // 关中断保护

    uint64 cur_ns = get_uptime_ns();

    task_t *curr = g_rq.cur_task;

    // 1. 记账：算好个人的醒来时间，并把自己的状态改为“睡觉”
    uint64 wake_ns = cur_ns+ (delay_us * 1000ULL);
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

    // 3. 强制交出麦克风！
    // 不管别人死活，我自己要睡了，调用 schedule 切给下一个人！
    schedule();

    // --- (漫长的等待...) ---
    // 4. 等未来某一天时钟中断触发，Timer ISR 把你从红黑树里摘下来，
    // schedule() 才会返回，代码才会走到这里！
    local_irq_restore(flags);
}

/**
 * @brief 动态重置硬件闹钟 (Tickless 核心引擎)
 * @details 负责在任务切换或中断结束时，精准计算下一次闹钟时间
 */
void reprogram_timer_for_next_event(uint64 cur_ns) {
    uint64 sleep_deadline = 0xFFFFFFFFFFFFFFFFULL; // MAX
    uint64 sched_deadline = 0xFFFFFFFFFFFFFFFFULL; // MAX

    // 1. 扫描睡觉区：最近的唤醒死线 (Clock A)
    rb_node_t *node = rb_first(&g_rq.sleep_tree);
    if (node != NULL) {
        sleep_deadline = (CONTAINER_OF(node, task_t, sleep_node))->wake_up_ns;
    }

    // 2. 扫描干活区：当前任务的剥夺死线 (Clock B)
    task_t *curr = g_rq.cur_task;
    if (curr && curr != g_rq.idle_task) {
        // 如果当前任务没被贴标签，且还有虚拟时间余额
        if (!(curr->flags & TIF_NEED_RESCHED) && (curr->v_deadline > curr->v_eligible)) {
            // 反推物理余额：剩余虚拟时间 -> 剩余物理时间
            uint64 v_left = curr->v_deadline - curr->v_eligible;
            uint64 phys_left = (v_left * curr->weight) / NICE_0_LOAD;
            // 🌟 千万别忘了 50us 硬件防线
            if (phys_left < 50000ULL) phys_left = 50000ULL;
            sched_deadline = cur_ns + phys_left;
        }
    }

    // 3. 终极裁决：谁离现在最近，APIC 就听谁的！
    uint64 final_deadline = (sleep_deadline < sched_deadline) ? sleep_deadline : sched_deadline;
    // 如果final_deadline = 0xFFFFFFFFFFFFFFFF 则可以关闭定时器进入了。
    reprogram_clockevent(final_deadline);
}

/**
 * @brief 扫描睡眠树和调度树，唤醒所有到期的任务，并重设下一个硬件闹钟
 */
#define EARLY_WAKEUP_TOLERANCE_NS 2000ULL // 容差窗口：2微秒
int32 timer_irq_handler (cpu_registers_t *regs,void *dev_id) {
    uint64 cur_ns = get_uptime_ns();
    task_t *cur_task = g_rq.cur_task;

    // ========================================================
    // 调度任务时间结算
    // ========================================================
    if (cur_task && cur_task != g_rq.idle_task) {
        update_cur_task(cur_ns);
        if (cur_task->v_eligible >= cur_task->v_deadline) {
            cur_task->flags |= TIF_NEED_RESCHED;
        }
    }

    // ========================================================
    // 定时任务时间结算
    // ========================================================
    uint64 effective_now = cur_ns + EARLY_WAKEUP_TOLERANCE_NS;
    rb_node_t *node;
    while ((node = rb_first(&g_rq.sleep_tree)) != NULL) {
        task_t *sleep_task = CONTAINER_OF(node, task_t, sleep_node);
        if (sleep_task->wake_up_ns > effective_now) break;      //定时树最近的时间都未到直接退出

        rb_erase(&g_rq.sleep_tree, &sleep_task->sleep_node, NULL);  //定时到了拔出任务
        sleep_task->state = TASK_READY;     //设置就绪状态
        enqueue_task_eevdf(sleep_task);     //插入就绪树等待调度

        if (sleep_task != cur_task) {
            cur_task->flags |= TIF_NEED_RESCHED;
        }
    }

    reprogram_timer_for_next_event(cur_ns);
}



