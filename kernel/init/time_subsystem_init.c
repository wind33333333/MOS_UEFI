//////////////////////////////////
// =========================================================================
// 系统时钟与定时器子系统总控初始化 (在 0号核 BSP 上调用)
// =========================================================================
void time_subsystem_init(hpet_device_t *hpet_dev, uint16 acpi_pm_ioport, uint32 max_basic_leaf) {
    // ---------------------------------------------------------------------
    // 第 1 步：依次探测并注册所有可用的【时钟源 (表盘)】
    //         系统会自动根据 rating 选出最优者 (TSC > HPET > ACPI_PM)
    // ---------------------------------------------------------------------
    if (acpi_pm_ioport != 0) {
        g_cs_acpi_pm.priv = (void *)(uintptr_t)acpi_pm_ioport;
        clocksource_register(&g_cs_acpi_pm); // 先挂上保底的 ACPI_PM (rating 150)
    }

    if (hpet_dev != NULL) {
        g_cs_hpet.freq_hz = hpet_dev->frequency_hz;
        g_cs_hpet.priv    = hpet_dev;
        clocksource_register(&g_cs_hpet);    // 自动升级切换到 HPET (rating 250)
    }

    uint64 tsc_hz = detect_tsc_hz(hpet_dev, max_basic_leaf);
    if (tsc_hz != 0) {
        g_cs_tsc.freq_hz = tsc_hz;
        clocksource_register(&g_cs_tsc);     // 校准完成，自动无缝升级到 TSC (rating 400)！
    }

    // ---------------------------------------------------------------------
    // 第 2 步：探测并注册所有可用的【定时事件设备 (闹钟)】
    //         注意：即使在 Intel CPU 上，我们也把 One-Shot 一并注册进去，方便随时手动切换测试！
    // ---------------------------------------------------------------------
    // 2.1 探测并注册 APIC One-Shot 驱动 (rating 350)
    uint64 raw_bus_hz = detect_apic_bus_hz(max_basic_leaf, tsc_hz);
    static const uint8 k_div_table[8] = {0x0B, 0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A};
    uint32 shift = 0;
    while (shift < 7 && (raw_bus_hz >> shift) > 10000000ULL) {
        shift++;
    }
    g_apic_div_cfg             = k_div_table[shift];
    g_ce_lapic_oneshot.freq_hz = raw_bus_hz >> shift;
    clockevent_register(&g_ce_lapic_oneshot);

    // 2.2 若硬件支持 TSC-Deadline，注册更高优先级的 Deadline 驱动 (rating 450)
    if (cpu_has_tsc_deadline()) {
        g_ce_lapic_deadline.freq_hz = tsc_hz;
        clockevent_register(&g_ce_lapic_deadline);
    }
}

// =========================================================================
// 每个 CPU 核心启动自己的本地定时器 (BSP 与 AP 均调用此函数)
// =========================================================================
void time_init_per_cpu(void) {
    cpu_core_t *core = &cpu_cores[THIS_CPU->logical_id];

    // 自动从已注册的定时器池中挑选 rating 最高的一个激活
    clockevent_t *best_ce = NULL;
    for (uint32 i = 0; i < g_ce_count; i++) {
        if (best_ce == NULL || g_ce_registry[i]->rating > best_ce->rating) {
            best_ce = g_ce_registry[i];
        }
    }

    // 设定初始 1ms (1,000,000 ns) 后的唤醒目标，并激活硬件定时器
    core->next_deadline_ns = get_uptime_ns() + 1000000ULL;
    clockevent_switch_this_cpu(best_ce);
}