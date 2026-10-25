#include "../time/timer.h"
#include "../x64/interrupt_dispatcher.h"
#include "time_core.h"
#include "../sched/sched.h"
#include "../sched/task.h"
#include "../x64/cpu.h"

void ksleep_us(uint64 delay_us) {
    uint64 flags;
    local_irq_save(&flags);

    uint64 cur_ns = get_uptime_ns();
    task_t *cur_task = THIS_CPU->cur_task;
    sleep_queue_t *sleep_queue = &this_cpu()->sleep_queue;

    // 1. 记账
    uint64 wake_ns = cur_ns + (delay_us * 1000ULL);
    cur_task->wake_up_ns = wake_ns;
    cur_task->state = TASK_SLEEPING;

    // 2. 挂入红黑树并实时维护 rb_leftmost
    rb_node_t **link = &sleep_queue->sleep_tree.rb_node;
    rb_node_t *parent = NULL;
    boolean leftmost = TRUE; // 🌟 追踪是否一路向左

    while (*link) {
        parent = *link;
        task_t *parent_task = CONTAINER_OF(parent, task_t, sleep_node);

        if (cur_task->wake_up_ns < parent_task->wake_up_ns) {
            link = &parent->left;
        } else {
            link = &parent->right;
            leftmost = FALSE; // 🌟 只要发生过一次向右寻址，它就绝不可能是最小节点
        }
    }

    rb_insert(&sleep_queue->sleep_tree, &cur_task->sleep_node, parent, link, NULL);

    // 🌟 若为新的最小节点，O(1) 刷新缓存指针与死线
    if (leftmost) {
        sleep_queue->rb_leftmost = &cur_task->sleep_node;
        sleep_queue->next_wake_ns = wake_ns;
    }

    // 3. 让出 CPU
    schedule();

    local_irq_restore(flags);
}

int64 sys_ksleep_us(cpu_registers_t *regs) {
    ksleep_us(regs->rdi);
    return 0;
}

/**
 * @brief 动态重置硬件闹钟 (Tickless 核心引擎)
 * @details 负责在任务切换或中断结束时，精准计算下一次闹钟时间
 */
void reprogram_timer_for_next_event(uint64 cur_ns) {
    // 1. 获取定时器睡眠树的最早到期时间
    uint64 sleep_deadline = THIS_CPU->sleep_queue.next_wake_ns;

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
    (void)regs, (void)dev_id;
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
    // 2. 注册统一的中断处理入口
    for (uint32 i = 0 ; i < cpu_info.total_cpu_count;i++) {
        register_irq(i,APIC_TIMER_VECTOR,timer_irq_handler,NULL,"sys-timer");
    }

}



