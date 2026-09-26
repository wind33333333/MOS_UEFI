#pragma once
#include "moslib.h"
#include "../x64/msr.h"
#include "../x64/cpu.h"

static inline void set_gs_base(uint32 logical_id) {
    asm_wrmsr(KERNEL_GS_BASE_MSR,0);
    asm_wrgsbase(&cpu_cores[logical_id]);
}

// 根据物理 APIC ID 反查逻辑 ID（仅在启动期解析 SRAT 等冷路径调用）
static inline uint32 get_logical_id_by_apic(uint32 apic_id) {
    for (uint32 i = 0; i < active_cpu_count; i++) {
        if (cpu_cores[i].apic_id == apic_id) {
            return i; // 找到了对应的逻辑编号
        }
    }
    return 0xFFFFFFFF; // 未找到（无效或被禁用的核心）
}

void cpu_enable_feature(void);
void cpu_alloc_resources(void);
void cpu_load_resource(void);
void ap_init(void);
void ap_main(void);
