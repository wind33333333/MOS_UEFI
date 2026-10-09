#pragma once
#include "interrupt.h"
#include "moslib.h"



// 任务微秒休眠
void sleep_us(uint64 delay_us);

// 任务毫秒休眠
static inline void sleep_ms(uint64 delay_ms) {
    sleep_us(delay_ms * 1000ULL);
}

// 供调度器主动切栈时更新物理闹钟
void reprogram_timer_for_next_event(uint64 cur_ns);

// 时钟中断总服务程序
int32 timer_irq_handler(cpu_registers_t *regs, void *dev_id);