#pragma once
#include "moslib.h"

#define APIC_ONESHOT        (0 << 17)              //一次性定时模式
#define APIC_PERIODIC       (1 << 17)              //周期性定时模式
#define APIC_TSC_DEADLINE   (2 << 17)              //TSC截止期限模式
#define APIC_DIV_BY_1       0x0B

typedef struct {
    uint8  div_cfg;       // 算好的全局统一分频配置 (如 0x03)
    // 未来如果需要，还可以放更多 APIC 专有状态，比如 calibration_multiplier 等
} apic_timer_ctx_t;

void apic_time_init(void);


