#include "clocksource.h"

static timekeeper_t   g_timekeeper;
static clocksource_t *g_cs_registry[MAX_CLOCKSOURCES];
static uint32         g_cs_count = 0;

// 内部辅助：读取指定时钟源的当前计数值
static inline uint64 cs_read_cycles(clocksource_t *cs) {
    return __builtin_expect(cs->is_tsc, 1) ? asm_rdtscp() : cs->read(cs);
}

// =========================================================================
// 3. 获取系统单调运行时间 (纳秒) —— 直接通过局部快照 cs 访问静态属性
// =========================================================================
static inline uint64 get_uptime_ns(void) {
    uint32 seq;
    uint64 base, last, now_cycles;
    clocksource_t *cs;

    do {
        seq = g_timekeeper.seq;
        asm_lfence();

        // 在顺序锁保护下，一次性拍下锚点三要素的同步快照
        base = g_timekeeper.base_ns;
        last = g_timekeeper.cycle_last;
        cs   = g_timekeeper.active_cs;

        now_cycles = cs_read_cycles(cs);

        asm_lfence();
    } while (__builtin_expect((seq & 1U) || (g_timekeeper.seq != seq), 0));

    // 循环退出后，cs 指针已确保与 base、last 严格对应，直接解引用 cs 的只读常数即可！
    uint64 delta_cycles = (now_cycles - last) & cs->mask;
    uint64 delta_ns     = (uint64)(((__uint128_t)delta_cycles * cs->mult) >> cs->shift);

    return base + delta_ns;
}

// =========================================================================
// 4. 运行时动态切换时钟源 (临界区精简至仅需修改 3 个字段！)
// =========================================================================
boolean clocksource_switch(clocksource_t *new_cs) {
    if (new_cs == NULL || new_cs == g_timekeeper.active_cs) {
        return FALSE;
    }

    uint64 flags;
    local_irq_save(&flags);

    // 1. 用旧时钟源结算到当前时刻的累积纳秒数
    uint64 current_ns = 0;
    clocksource_t *old_cs = g_timekeeper.active_cs;
    if (old_cs != NULL) {
        uint64 old_now   = cs_read_cycles(old_cs);
        uint64 old_delta = (old_now - g_timekeeper.cycle_last) & old_cs->mask;
        current_ns = g_timekeeper.base_ns +
                     (uint64)(((__uint128_t)old_delta * old_cs->mult) >> old_cs->shift);
    }

    // 2. 采样新时钟源的起点计数值
    uint64 new_cycle_start = cs_read_cycles(new_cs);

    // 3. 顺序锁保护下，仅需更新 3 个核心锚点字段！
    g_timekeeper.seq++;
    asm_mfence();

    g_timekeeper.base_ns    = current_ns;
    g_timekeeper.cycle_last = new_cycle_start;
    g_timekeeper.active_cs  = new_cs;

    asm_mfence();
    g_timekeeper.seq++;

    local_irq_restore(flags);
    return TRUE;
}

// 按名称在运行时手动切换时钟源，例如：clocksource_switch_by_name("hpet")
boolean clocksource_switch_by_name(const char *name) {
    for (uint32 i = 0; i < g_cs_count; i++) {
        if (strcmp(g_cs_registry[i]->name, name) == 0) {
            return clocksource_switch(g_cs_registry[i]);
        }
    }
    return FALSE;
}

// 注册新时钟源：自动计算 mult/shift，若评分高于当前时钟源则自动切换升级！
void clocksource_register(clocksource_t *cs) {
    calc_mult_shift(cs->freq_hz, 1000000000ULL, &cs->mult, &cs->shift, FALSE);
    g_cs_registry[g_cs_count++] = cs;

    if (g_timekeeper.active_cs == NULL || cs->rating > g_timekeeper.active_cs->rating) {
        clocksource_switch(cs);
    }
}

// 周期性刷新时间锚点 (建议由 0号核每秒调用 1 次，防止 24位 ACPI_PM 在 4.68 秒后回绕溢出)
void timekeeper_refresh_anchor(void) {
    if (g_timekeeper.active_cs != NULL) {
        clocksource_switch(g_timekeeper.active_cs); // 原地自切换即可无损推进 base_ns 与 cycle_last！
    }
}


////////////////////////

// --- [驱动 1] TSC 时钟源 ---
static uint64 tsc_cs_read(clocksource_t *cs) { (void)cs; return asm_rdtscp(); }
static clocksource_t g_cs_tsc = {
    .name   = "tsc",
    .rating = 400,
    .mask   = CS_MASK_64BIT,
    .is_tsc = TRUE,
    .read   = tsc_cs_read
};

// --- [驱动 2] HPET 时钟源 ---
static uint64 hpet_cs_read(clocksource_t *cs) {
    hpet_device_t *hpet = (hpet_device_t *)cs->priv;
    return hpet->hw_regs->main_counter;
}
static clocksource_t g_cs_hpet = {
    .name   = "hpet",
    .rating = 250,
    .mask   = CS_MASK_64BIT, // 若检测到 32 位 HPET 可设为 CS_MASK_32BIT
    .is_tsc = FALSE,
    .read   = hpet_cs_read
};

// --- [驱动 3] ACPI PM Timer 时钟源 (恒定 3.579545 MHz) ---
#define ACPI_PM_FREQ_HZ  3579545ULL
static uint64 acpi_pm_cs_read(clocksource_t *cs) {
    uint16 ioport = (uint16)(uintptr_t)cs->priv;
    return (uint64)asm_inl(ioport); // 读取 FADT PM_TMR_BLK 32位端口
}
static clocksource_t g_cs_acpi_pm = {
    .name    = "acpi_pm",
    .rating  = 150,
    .freq_hz = ACPI_PM_FREQ_HZ,
    .mask    = CS_MASK_24BIT, // 默认按 24 位掩码保护 (若 FADT TMR_VAL_EXT=1 则改为 CS_MASK_32BIT)
    .is_tsc  = FALSE,
    .read    = acpi_pm_cs_read
};