#pragma once
#include "../x64/cpu_registers.h"
#include "moslib.h"


// 任务微秒休眠
void ksleep_us(uint64 delay_us);

// 任务毫秒休眠
static inline void ksleep_ms(uint64 delay_ms) {
    ksleep_us(delay_ms * 1000ULL);
}

int64 sys_ksleep_us(cpu_registers_t *regs);

// 供调度器主动切栈时更新物理闹钟
void reprogram_timer_for_next_event(uint64 cur_ns);

void timer_irq_init(void);