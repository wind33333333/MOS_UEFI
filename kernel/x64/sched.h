#pragma once
#include "moslib.h"
#include "rbtree.h"
#include "task.h"

// 调度队列 (Per-CPU)
typedef struct {
    task_t *cur_task;            // 当前正在 CPU 上飞驰的任务
    task_t *idle_task;          // 兜底的系统空闲任务 (hlt)
    rb_root_t sched_tree;       // EEVDF就绪任务红黑树 (以 Ve 为 Key)
    rb_root_t sleep_tree;       // 睡眠红黑树 (以 wake_up_ns 为 Key)
    uint64 vtime;               // 系统当前的全局虚拟时间 (V)
} runqueue_t;

extern runqueue_t g_rq;

// 调度器核心 API
void schedule();
void sched_tick(uint64 cur_ns);
void sched_wake_up(uint64 cur_ns);
uint64 sched_get_slice_deadline(uint64 cur_ns);




