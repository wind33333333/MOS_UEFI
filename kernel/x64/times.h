#include "../include/moslib.h"

//时钟
typedef struct clock_t {
    char *name;                 // 时钟名称，如 "tsc", "hpet", "acpi_pm"
    uint64 rating;            // 🌟 架构核心：评分！分数越高的内核优先用
    uint64 frequency_hz;      // 该时钟的绝对频率
    uint64 mask;              // 掩码 (因为有的是32位，有的是64位)

    uint32 mult;
    uint32 shift;

    // 抽象的方法指针 (面向对象思想在 C 语言的应用)
    uint64 (*read)(struct clock_t *cs);
    void   (*enable)(struct clock_t *cs);

    struct clock_t *next;   // 链表指针，用于串联所有可用时钟
}clock_t;

//定时器
typedef struct timer_t {
    char *name;                 // 如 "lapic", "hpet_comp", "pit"
    uint64 rating;            // 评分
    uint64 frequency_hz;      // 定时器频率
    uint32 features;          // 特性标志：比如支持 ONE_SHOT, PERIODIC, TSC_DEADLINE

    uint32 mult;
    uint32 shift;

    // 设置下一次触发的滴答数 (用于传统倒数模式)
    int (*set_next_event)(uint64 ticks, struct timer_t *dev);
    // 设置绝对死线 (用于 TSC-Deadline 模式)
    int (*set_next_ktime)(uint64 absolute_time, struct timer_t *dev);

    struct timer_t *next;
}timer_t;

