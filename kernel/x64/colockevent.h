#pragma once
#include "moslib.h"

#define MAX_CLOCKEVENTS  8

// =========================================================================
// 1. 抽象定时事件驱动接口 (Clockevent Device)
// =========================================================================
typedef struct clockevent {
    const char *name;               // "lapic_deadline", "lapic_oneshot", "hpet_timer"
    uint32      rating;             // 评分：Deadline(450) > OneShot(350) > HPET(200)
    uint64      freq_hz;            // 定时器硬件工作频率 (Deadline 模式下即为 tsc_hz)

    // 预计算的 纳秒(ns) -> 硬件 Tick 定点数参数 (全系驱动通用，零除法！)
    uint64      ns_to_dev_mult;
    uint64      ns_to_dev_mask;
    uint32      ns_to_dev_shift;

    // 硬件操作回调
    void      (*init_hw)(struct clockevent *ce);                   // 开启当前硬件定时器通道
    void      (*stop_hw)(struct clockevent *ce);                   // 停表并屏蔽当前定时器中断
    void      (*set_next_delay_ns)(struct clockevent *ce, uint64 delay_ns); // 设定单次闹钟
    void       *priv;
} clockevent_t;
