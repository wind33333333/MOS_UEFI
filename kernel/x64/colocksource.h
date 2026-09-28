#pragma once
#include "moslib.h"

#define CS_MASK_64BIT           0xFFFFFFFFFFFFFFFFULL
#define CS_MASK_32BIT           0x00000000FFFFFFFFULL
#define CS_MASK_24BIT           0x0000000000FFFFFFULL
#define MAX_CLOCKSOURCES        8

// =========================================================================
// 1. 抽象时钟源驱动接口 (每个硬件时钟实现一个此结构体)
// =========================================================================
typedef struct clocksource {
    const char *name;           // 设备名："tsc", "hpet", "acpi_pm"
    uint32      rating;         // 优先级评分：越高越优先 (TSC=400, HPET=250, ACPI_PM=150)
    uint64      freq_hz;        // 硬件计数频率 (Hz)
    uint64      mask;           // 位宽掩码 (64位为全F，24位 ACPI_PM 为 0xFFFFFF)

    uint64      mult;           // 预计算的 ticks -> ns 乘法系数
    uint32      shift;          // 预计算的 ticks -> ns 右移位数
    boolean     is_tsc;         // 是否为原生 TSC (用于开启内联极速通道)

    uint64    (*read)(struct clocksource *cs); // 读取硬件当前计数值
    void       *priv;           // 驱动私有指针 (如指向 hpet_device_t 或 IO 端口)
} clocksource_t;

// =========================================================================
// 2. 全局活动时间锚点 (独占 1 条 64B 缓存行，热路径直接访问，无需二次解引用)
// =========================================================================
typedef struct {
    volatile uint32 seq;            // 顺序锁：偶数为稳定态，奇数为正在切换/更新锚点
    boolean         is_tsc_fast;    // TRUE 时直接内联执行 rdtscp，跳过函数指针调用

    uint64          base_ns;        // 锚点时刻已累积的单调纳秒数
    uint64          cycle_last;     // 锚点时刻硬件计数器的读数快照
    uint64          mask;           // 当前时钟源位宽掩码
    uint64          mult;           // 当前时钟源 mult
    uint32          shift;          // 当前时钟源 shift

    clocksource_t  *active_cs;      // 指向当前激活的时钟源驱动
} __attribute__((aligned(64))) timekeeper_t;