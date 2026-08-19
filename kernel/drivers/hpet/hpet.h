#pragma once

#include "../../include/moslib.h"
#include "../x64/times.h"


#define ENABLE_HPET_TIMES(TIMS_CONF,TIMS_COMP,TIME,MODEL,IRQ) \
        do {   \
           (TIMS_CONF) = (((IRQ) << 9) | (1UL << 6) | ((MODEL) << 3) | (1UL << 2)); \
           MFENCE();                           \
           (TIMS_COMP) = (TIME);                                                  \
           MFENCE();                                 \
         }while(0)

#define DISABLE_HPET_TIMES(TIMS_CONF) \
        do {                 \
           (TIMS_CONF) = 0;           \
           MFENCE();\
        }while(0)

#define HPET_ONESHOT 0
#define HPET_PERIODIC 1


// ---------------------------------------------------------
// 1. HPET 定时器通道硬件寄存器 (Timer N)
// 规范：从 0x100 开始，每个 Timer 占用 0x20 (32字节)
// ---------------------------------------------------------
typedef struct {
    volatile uint64 config_cap;   // 0x00: 配置与能力寄存器
    volatile uint64 comparator;   // 0x08: 比较器目标值寄存器
    volatile uint64 fsb_route;    // 0x10: FSB 中断路由寄存器 (用于MSI)
    uint64          reserved;     // 0x18: 保留，用于对齐 0x20 边界
} __attribute__((packed)) hpet_hw_timer_t;


// ---------------------------------------------------------
// 2. HPET 全局硬件寄存器映射 (Total MMIO Map)
// 规范：整个结构体完美映射从 0x00 到 0x3FF 的物理地址
// ---------------------------------------------------------
typedef struct {
    volatile uint64 general_cap_id;       // 0x000: 全局能力与 ID 寄存器
    uint64          reserved0;            // 0x008: 保留

    volatile uint64 general_config;       // 0x010: 全局配置寄存器 (Enable 等)
    uint64          reserved1;            // 0x018: 保留

    volatile uint64 general_int_status;   // 0x020: 全局中断状态寄存器
    uint8           reserved2[200];       // 0x028 ~ 0x0EF: 保留填充 (200 bytes)

    volatile uint64 main_counter;         // 0x0F0: 🌟 核心：主计数器当前值
    uint64          reserved3;            // 0x0F8: 保留

    // 0x100 开始是独立的定时器阵列，规范规定最多 32 个
    hpet_hw_timer_t   timers[32];
} __attribute__((packed)) hpet_hw_regs_t;



// ---------------------------------------------------------
// 3. 逻辑通道抽象 (Logical Channel Context)
// 用于记录每个 Timer 的能力、中断路由和软件回调
// ---------------------------------------------------------
typedef struct {
    uint8  id;                    // 通道编号 (0 ~ 31)
    boolean     is_present;            // 硬件是否真实存在该通道

    // 硬件能力缓存 (从 config_cap 读出)
    boolean     supports_64bit;        // 是否支持 64 位比较器
    boolean     supports_periodic;     // 是否支持周期性触发模式
    uint32 allowed_irq_bitmap;    // 允许被路由到的 IOAPIC IRQ 引脚位图

    // 软件运行状态
    boolean     is_enabled;            // 当前是否正在运行
    uint8  assigned_irq;          // 当前分配的 IRQ 号

    // 每个通道都是一个独立的闹钟，它们各自向相应的 CPU 核心报到！
    timer_t timer;

    // 回调钩子 (当该通道触发中断时，执行此函数)
    void     (*isr_callback)(void *ctx);
    void     *callback_ctx;
} hpet_timer_t;


// ---------------------------------------------------------
// 4. HPET 全局设备抽象 (Global Device Context)
// 微内核中的 "HPET 驱动对象"
// ---------------------------------------------------------
typedef struct {
    // === 物理与虚拟内存信息 ===
    uint64       phys_base_addr; // 物理基地址 (如 0xFED00000，从 ACPI 取)
    hpet_hw_regs_t *hw_regs;        // 经过 MMU 映射后的虚拟地址指针

    // === 核心时间属性 (只读缓存) ===
    uint32        period_fs;      // 时钟周期 (飞秒)
    uint64        frequency_hz;   // 🌟 算好的物理频率 (如 19200000 Hz)
    boolean            supports_64bit; // 主计数器是否原生 64 位
    boolean            legacy_routing; // 是否支持替换 PIT/RTC (Legacy Route)

    // === 运行状态 ===
    boolean            is_running;     // ENABLE_CNF 是否已置位

    // === 子节点管理 ===
    uint8         num_timers;     // 拥有的有效定时器数量 (如 3)
    hpet_timer_t  hpet_timers[32];   // 独立的通道上下文数组

    // === 🌟 全局时钟源抽象 ===
    // 时钟源(看表)是全局的，整个 HPET 作为一个表
    clock_t       clock;

    // === 内核同步 ===
    // spinlock_t   lock;           // 保护硬件寄存器并发写入的自旋锁
} hpet_device_t;


extern hpet_device_t hpet_dev;

void init_hpet(void);
uint64 hpet_calibrate_tsc_hz(hpet_device_t *hpet_dev, uint32 wait_ms);
uint64 hpet_calibrate_apic_hz(hpet_device_t *hpet_dev, uint32 wait_ms);