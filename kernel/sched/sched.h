#pragma once
#include "moslib.h"
#include "rbtree.h"
#include "../x64/cpu.h"
#include "../sched/sched_eevdf.h"
#include "../sched/task.h"
#include "../time/time_core.h"
#include "../time/timer.h"


static inline void sched_tick(uint64 cur_ns) {
    task_t *cur_task = THIS_CPU->cur_task;
    if (!cur_task && cur_task->id == 0) return;
    update_cur_task_eevdf(cur_ns);
    if (cur_task->v_eligible >= cur_task->v_deadline) {
        cur_task->flags |= TIF_NEED_RESCHED;
    }
}

#define EARLY_WAKEUP_TOLERANCE_NS 2000ULL // 2微秒提前容差，防御时钟抖动
static inline void sched_wake_up(uint64 cur_ns) {
    sleep_queue_t *sleep_queue = &this_cpu_ptr()->sleep_queue;
    uint64 effective_now = cur_ns + EARLY_WAKEUP_TOLERANCE_NS;

    // 🌟 O(1) 短路拦截：最近的定时任务都没到期，完全不碰红黑树
    if (sleep_queue->next_wake_ns > effective_now) return;

    run_queue_t *run_queue = &this_cpu_ptr()->run_queue;

    // 🌟 O(1) 消费最左节点，彻底消灭 rb_first 循环遍历
    while (sleep_queue->rb_leftmost != NULL) {
        rb_node_t *node = sleep_queue->rb_leftmost;
        task_t *sleep_task = CONTAINER_OF(node, task_t, sleep_node);

        if (sleep_task->wake_up_ns > effective_now) {
            sleep_queue->next_wake_ns = sleep_task->wake_up_ns;
            break;
        }

        // 🌟 核心算法：在拔出当前最左节点前，通过 rb_next 均摊 O(1) 锁定下一个最左候选
        rb_node_t *next_node = rb_next(node);
        rb_erase(&sleep_queue->sleep_tree, node, NULL);
        sleep_queue->rb_leftmost = next_node;

        // 任务状态扭转与 EEVDF 钳位
        sleep_task->state = TASK_READY;
        if (sleep_task->v_eligible < run_queue->vtime) {
            sleep_task->v_eligible = run_queue->vtime;
        }
        enqueue_task_eevdf(sleep_task);

        if (sleep_task != THIS_CPU->cur_task) {
            THIS_CPU->cur_task->flags |= TIF_NEED_RESCHED;
        }
    }

    // 若全部定时器均已出队，将闹钟推至无限远
    if (sleep_queue->rb_leftmost == NULL) {
        sleep_queue->next_wake_ns = TIME_MAX_NS;
    }
}

static inline uint64 sched_get_slice_deadline(uint64 cur_ns) {
    task_t *cur_task = THIS_CPU->cur_task;
    if (!cur_task || cur_task->id == 0) return TIME_MAX_NS;

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



