#include "../sched/sched.h"
#include "../sched/sched_eevdf.h"


void context_switch(uint64 *prev_rsp, uint64 *next_rsp);



/**
 * @brief 主调度器接管点
 */
void schedule() {
    uint64 flags;
    local_irq_save(&flags);

    uint64 cur_ns = get_uptime_ns();

    task_t *prev = g_rq.cur_task;

    // 1. 结清前一个任务的时间账单
    update_cur_task_eevdf(cur_ns);
    prev->flags &= ~TIF_NEED_RESCHED; // 撕下便签

    // 2. 状态分发
    if (prev->state == TASK_RUNNING) {
        // 如果依然是 running，说明是时间片耗尽被强行切下的，重新入队继续排
        enqueue_task_eevdf(prev);
    }
    // 如果是 TASK_SLEEPING，不入队，它已经在定时器树里了

    // 3. 选出下一个天命之子
    task_t *next = pick_next_task_eevdf();
    if (!next) {
        next = g_rq.idle_task;
    } else {
        dequeue_task_eevdf(next); // 离开就绪队列，独占 CPU
    }

    next->state = TASK_RUNNING;
    next->last_update_time = cur_ns;
    g_rq.cur_task = next;

    // ========================================================
    // 🌟 终极修复：换人后，决不能继承旧闹钟！
    // 因为 next 刚刚出列，它的 v_deadline 是重新充满的 10ms，
    // 调用这个引擎，APIC 就会极其精准地被设定在 10ms 之后！
    // ========================================================
    reprogram_timer_for_next_event(cur_ns);

    // 4. 底层汇编硬切换
    if (prev != next) {
        context_switch(&prev->rsp, &next->rsp);
    }

    local_irq_restore(flags);
}

void check_and_schedule() {
    // 如果当前任务被贴了“换人”的条子
    if (g_rq.cur_task->flags & TIF_NEED_RESCHED) {
        g_rq.cur_task->flags &= ~TIF_NEED_RESCHED; // 撕掉条子

        // 🌟 在这里进行真正的上下文切换！
        // 即使栈在这里被劫持，APIC 也绝对不会死锁，因为 EOI 早就发完了！
        schedule();
    }
}



