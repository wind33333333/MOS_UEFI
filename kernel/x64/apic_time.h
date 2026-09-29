#pragma once
#include "moslib.h"

typedef struct {
    uint8  div_cfg;       // 算好的全局统一分频配置 (如 0x03)
    uint8  irq_vector;    // 动态分配的全局统一中断号 (如 0x20)
    // 未来如果需要，还可以放更多 APIC 专有状态，比如 calibration_multiplier 等
} apic_timer_ctx_t;

void apic_time_init(void);