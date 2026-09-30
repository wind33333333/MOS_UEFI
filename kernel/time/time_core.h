#pragma once
#include "moslib.h"

#define CS_MASK_64BIT  0xFFFFFFFFFFFFFFFFULL
#define CS_MASK_32BIT  0x00000000FFFFFFFFULL
#define CS_MASK_24BIT  0x0000000000FFFFFFULL

// 1. 抽象时钟源接口 (表盘驱动各自实例化)
typedef struct clocksource_t {
    const char *name;
    uint32      rating;
    uint64      freq_hz;
    uint64      mask;

    uint64      mult;
    uint32      shift;
    boolean     is_tsc;

    uint64    (*read)(struct clocksource_t *cs);
    void       *priv;
} clocksource_t;

// 2. 抽象定时事件接口 (闹钟驱动各自实例化)
typedef struct clockevent_t {
    const char *name;
    uint32      rating;
    uint64      freq_hz;

    uint64      ns_to_dev_mult;
    uint64      ns_to_dev_mask;
    uint32      ns_to_dev_shift;

    void      (*init_hw)(struct clockevent_t *ce);
    void      (*stop_hw)(struct clockevent_t *ce);
    void      (*set_next_delay_ns)(struct clockevent_t *ce, uint64 delay_ns);
    void       *priv;
} clockevent_t;

// 供各硬件驱动主动调用的注册 API
void    clocksource_register(clocksource_t *cs);
boolean clocksource_switch_by_name(const char *name);
uint64  clocksource_get_active_freq(void);

void    clockevent_register(clockevent_t *ce);
boolean clockevent_switch_by_name(const char *name);
void    clockevent_init_per_cpu(void);

// 🌟 核心子系统提供的通用抗 NMI/SMI 测频服务：
// 后启动的驱动 (如 TSC、APIC) 只需传入自己的计数器读取函数，子系统自动用当前 active_cs 为其测出频率！
uint64  timekeeping_measure_freq_hz(uint64 (*target_read)(void), boolean is_down_counter, uint32 wait_ms);

//时间系统初始化
void time_core_init(void);