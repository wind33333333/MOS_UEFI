#pragma once
#include "moslib.h"
#include "msr.h"
#include "cpu.h"

static inline void set_gs_base(uint32 logical_id) {
    asm_wrmsr(KERNEL_GS_BASE_MSR,0);
    asm_wrgsbase(&cpu_cores[logical_id]);
}

void cpu_resources_init(void);

void ap_init(void);
void ap_main(void);
void bsp_init(void);
uint64 cpu_feature_init(void);
