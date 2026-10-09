#include "../time/timer.h"
#include "../x64/interrupt.h"
#include "time_core.h"
#include "../sched/sched.h"
#include "../sched/task.h"

uint8 g_sys_timer_vector = 0; // 全局统一定时器中断号

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
    uint64 sleep_deadline = TIME_MAX_NS;

    // 1. 获取定时器睡眠树的最早到期时间
    rb_node_t *sleep_node = rb_first(&g_rq.sleep_tree);
    if (sleep_node != NULL) {
        sleep_deadline = (CONTAINER_OF(sleep_node, task_t, sleep_node))->wake_up_ns;
    }

    // 2. 🌟 优雅解耦：直接向调度器索取时间片物理死线，绝不触碰 EEVDF 内部逻辑！
    uint64 sched_deadline = sched_get_slice_deadline(cur_ns);

    // 3. 终极裁决：谁先到期就定谁
    uint64 final_deadline = (sleep_deadline < sched_deadline) ? sleep_deadline : sched_deadline;
    reprogram_clockevent(final_deadline);
}


/**
 * @brief 扫描睡眠树和调度树，唤醒所有到期的任务，并重设下一个硬件闹钟
 */
int32 timer_irq_handler(cpu_registers_t *regs, void *dev_id) {
    uint64 cur_ns = get_uptime_ns();

    // 1. 🌟 调用调度器协议：推进计费并判定时间片
    sched_tick(cur_ns);

    // 2. 纯粹处理定时睡眠队列超时
    sched_wake_up(cur_ns);

    // 3. 为下一个周期续命
    reprogram_timer_for_next_event(cur_ns);

    // 🌟 修复：明确返回成功
    return 0;
}


void timer_irq_init(void) {
    // 1. 子系统统一申请，终生不释放
    g_sys_timer_vector = alloc_irq();

    // 2. 注册统一的中断处理入口
    register_isr(g_sys_timer_vector, timer_irq_handler,NULL,"sys-timer-irq");
}



