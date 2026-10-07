#pragma once
#include "moslib.h"



//微秒定时器
void sleep_us(uint64 delay_us);

//毫秒定时器
static inline void sleep_ms (uint64 delay_ms) {
    sleep_us(delay_ms*1000);
}

void check_sched_and_sleep_tasks(void);

void reprogram_timer_for_next_event(void);