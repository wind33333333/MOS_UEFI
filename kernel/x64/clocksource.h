#pragma once
#include "moslib.h"

#define CS_MASK_64BIT           0xFFFFFFFFFFFFFFFFULL
#define CS_MASK_32BIT           0x00000000FFFFFFFFULL
#define CS_MASK_24BIT           0x0000000000FFFFFFULL
#define MAX_CLOCKSOURCES        8

// =========================================================================
// 1. 抽象时钟源驱动接口 (静态只读属性全部集中在此处)
// =========================================================================
typedef struct clocksource {
    const char *name;           // 设备名："tsc", "hpet", "acpi_pm"
    uint32      rating;         // 优先级评分：越高越优先 (TSC=400, HPET=250, ACPI_PM=150)
    uint64      freq_hz;        // 硬件计数频率 (Hz)
    uint64      mask;           // 位宽掩码 (64位为全F，24位 ACPI_PM 为 0xFFFFFF)

    uint64      mult;           // 预计算的 ticks -> ns 乘法系数
    uint32      shift;          // 预计算的 ticks -> ns 右移位数
    boolean     is_tsc;         // 是否为原生 TSC (开启内联 rdtscp 快速通道)

    uint64    (*read)(struct clocksource *cs);
    void       *priv;
} clocksource_t;


// =========================================================================
// 2. 瘦身后的全局活动时间锚点 (仅 32 字节，零冗余字段！)
// =========================================================================
typedef struct {
    volatile uint32 seq;        // [4B] 顺序锁：偶数为稳定态，奇数为正在切换/更新锚点
    // [4B 编译器自动对齐填充]
    uint64          base_ns;    // [8B] 锚点时刻已累积的单调纳秒数
    uint64          cycle_last; // [8B] 锚点时刻硬件计数器的读数快照
    clocksource_t  *active_cs;  // [8B] 指向当前激活的时钟源驱动
} __attribute__((aligned(64))) timekeeper_t;