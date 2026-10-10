#pragma once
#include "moslib.h"
#include "rbtree.h"
#include "../x64/cpu.h"
#include "../sched/sched_eevdf.h"
#include "../sched/task.h"
#include "../time/time_core.h"
#include "../time/timer.h"



static inline void sched_tick(uint64 cur_ns) {
    task_t *cur_task = g_rq.cur_task;
    if (cur_task && cur_task != g_rq.idle_task) {
        update_cur_task_eevdf(cur_ns);
        if (cur_task->v_eligible >= cur_task->v_deadline) {
            cur_task->flags |= TIF_NEED_RESCHED;
        }
    }
}


#define EARLY_WAKEUP_TOLERANCE_NS 2000ULL // 容差窗口：2微秒
static inline void sched_wake_up(uint64 cur_ns) {
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


static inline uint64 sched_get_slice_deadline(uint64 cur_ns) {
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


// 调度器核心 API
void schedule();
void check_and_schedule();



