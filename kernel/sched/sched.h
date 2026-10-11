#pragma once
#include "moslib.h"
#include "rbtree.h"
#include "../x64/cpu.h"
#include "../sched/sched_eevdf.h"
#include "../sched/task.h"
#include "../time/time_core.h"
#include "../time/timer.h"


// 调度队列 (Per-CPU)
typedef struct run_queue_t{
    task_t *cur_task;            // 当前正在 CPU 上飞驰的任务
    task_t *idle_task;          // 兜底的系统空闲任务 (hlt)
    rb_root_t sched_tree;       // EEVDF就绪任务红黑树 (以 Ve 为 Key)
    uint64 vtime;               // 系统当前的全局虚拟时间 (V)
}run_queue_t;


//定时队列
typedef struct sleep_queue_t{
    rb_root_t       sleep_tree;       // 睡眠红黑树 (以 wake_up_ns 为 Key)
    rb_node_t       *rb_leftmost;     // 缓存的最左红黑树节点指针
    uint64          next_wake_ns;
}sleep_queue_t;


static inline void sched_tick(uint64 cur_ns) {
    task_t *cur_task = THIS_CPU->run_queue->cur_task;
    task_t *idle_task = THIS_CPU->run_queue->idle_task;
    if (cur_task && cur_task != idle_task) {
        update_cur_task_eevdf(cur_ns);
        if (cur_task->v_eligible >= cur_task->v_deadline) {
            cur_task->flags |= TIF_NEED_RESCHED;
        }
    }
}


static inline void sched_wake_up(uint64 cur_ns) {
    sleep_queue_t *sleep_queue = THIS_CPU->sleep_queue;

    if (sleep_queue->next_wake_ns > cur_ns) return;  //如果当前有效时间小于定时器下一个唤醒时间则所有定时任务都没到期直接跳过找树

    run_queue_t *run_queue = THIS_CPU->run_queue;

    while (TRUE) {
        rb_node_t* node = rb_first(&sleep_queue->sleep_tree);
        if (node == NULL) {
            sleep_queue->next_wake_ns = TIME_MAX_NS;
            break;
        }

        task_t *sleep_task = CONTAINER_OF(node, task_t, sleep_node);
        if (sleep_task->wake_up_ns > cur_ns) {
            sleep_queue->next_wake_ns = sleep_task->wake_up_ns;
            break;
        }

        rb_erase(&sleep_queue->sleep_tree, &sleep_task->sleep_node, NULL);
        sleep_task->state = TASK_READY;        // 🌟 规范唤醒：把控制权交还给调度器，由调度器执行状态重置与虚拟时间钳位

        // 🌟 单向钳位：防止超额休眠带来的不当红利，保留合理的透支债务
        if (sleep_task->v_eligible < run_queue->vtime) {
            sleep_task->v_eligible = run_queue->vtime;
        }
        enqueue_task_eevdf(sleep_task);

        if (sleep_task != run_queue->cur_task) {
            run_queue->cur_task->flags |= TIF_NEED_RESCHED;
        }
    }

}


static inline uint64 sched_get_slice_deadline(uint64 cur_ns) {
    run_queue_t *run_queue = THIS_CPU->run_queue;
    task_t *cur_task = run_queue->cur_task;
    if (!cur_task || cur_task == run_queue->idle_task) return TIME_MAX_NS;

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



