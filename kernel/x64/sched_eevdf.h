#pragma once
#include "moslib.h"
#include "task.h"

void enqueue_task_eevdf(task_t *task);
void dequeue_task_eevdf(task_t *task);
task_t* pick_next_task_eevdf(void);
void update_cur_task_eevdf(uint64 cur_ns);