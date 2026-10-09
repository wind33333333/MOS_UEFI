#include "sched.h"
#include "sched_eevdf.h"
#include "../time/time_core.h"
#include "../x64/timer.h"

void context_switch(uint64 *prev_rsp, uint64 *next_rsp);

//系统空闲任务，系统看门狗
task_t idle_task = {
    .id = 0,
    .state = TASK_RUNNING,
    .flags = TIF_NEED_RESCHED
};

//就绪队列
runqueue_t g_rq = {
    .cur_task = &idle_task,
    .idle_task = &idle_task,
    .vtime = 0,
    .sched_tree = {NULL},
    .sleep_tree = {NULL}
}; // 假设单核，多核则是 Per-CPU 变量


void sched_tick(uint64 cur_ns) {
    task_t *cur_task = g_rq.cur_task;
    if (cur_task && cur_task != g_rq.idle_task) {
        update_cur_task_eevdf(cur_ns);
        if (cur_task->v_eligible >= cur_task->v_deadline) {
            cur_task->flags |= TIF_NEED_RESCHED;
        }
    }
}


uint64 sched_get_slice_deadline(uint64 cur_ns) {
    task_t *cur_task = g_rq.cur_task;
    if (!cur_task || cur_task == g_rq.idle_task) return TIME_MAX_NS;

    // 若已被标记换人，或者额度已用尽，不占用调度闹钟
    if ((cur_task->flags & TIF_NEED_RESCHED) || (cur_task->v_deadline <= cur_task->v_eligible)) {
        return TIME_MAX_NS;
    }

    uint64 v_left = cur_task->v_deadline - cur_task->v_eligible;
    uint64 phys_left = (v_left * cur_task->weight) >> NICE_0_SHIFT;

    // 50 微秒硬件安全窗
    if (phys_left < 50000ULL) phys_left = 50000ULL;
    return cur_ns + phys_left;
}

#define EARLY_WAKEUP_TOLERANCE_NS 2000ULL // 容差窗口：2微秒
void sched_wake_up(uint64 cur_ns) {
    uint64 effective_now = cur_ns + EARLY_WAKEUP_TOLERANCE_NS;
    rb_node_t *node;
    while ((node = rb_first(&g_rq.sleep_tree)) != NULL) {
        task_t *sleep_task = CONTAINER_OF(node, task_t, sleep_node);
        if (sleep_task->wake_up_ns > effective_now) break;

        rb_erase(&g_rq.sleep_tree, &sleep_task->sleep_node, NULL);

        // 🌟 规范唤醒：把控制权交还给调度器，由调度器执行状态重置与虚拟时间钳位
        sleep_task->state = TASK_READY;

        // 🌟 单向钳位：防止超额休眠带来的不当红利，保留合理的透支债务
        if (sleep_task->v_eligible < g_rq.vtime) {
            sleep_task->v_eligible = g_rq.vtime;
        }

        enqueue_task_eevdf(sleep_task);

        if (sleep_task != g_rq.cur_task) {
            g_rq.cur_task->flags |= TIF_NEED_RESCHED;
        }
    }
}


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





