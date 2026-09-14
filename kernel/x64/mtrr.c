#include "mtrr.h"

mtrr_state_t g_bsp_mtrr_state;

// ---------------------------------------------------------
// 核心备份函数 (由 BSP 也就是 Core 0 调用)
// ---------------------------------------------------------
void bsp_backup_mtrr_state(void) {
    uint64 cap;

    // 1. 读取 MTRR 能力寄存器
    cap = asm_rdmsr(MSR_MTRRcap);

    // 解析 VCNT (底部的 8 个 bit，表示可变 MTRR 的对数)
    g_bsp_mtrr_state.vcnt = cap & 0xFF;

    // 2. 备份全局默认类型与使能状态 (MSR_MTRRdefType)
    g_bsp_mtrr_state.def_type = asm_rdmsr(MSR_MTRRdefType);

    // 3. 循环备份所有的可变范围 MTRR (Variable-Range MTRRs)
    for (uint32 i = 0; i < g_bsp_mtrr_state.vcnt; i++) {
        g_bsp_mtrr_state.var[i].base = asm_rdmsr(MSR_MTRRphysBase(i));
        g_bsp_mtrr_state.var[i].mask = asm_rdmsr(MSR_MTRRphysMask(i));
    }

    // 4. 备份固定范围 MTRR (Fixed-Range MTRRs)
    // 严格的安全检查：只有当硬件支持 (cap Bit 8) 且当前已开启 (def_type Bit 10) 时才去读
    // MTRR_CAP_FIX = (1 << 8), MTRR_DEF_TYPE_FIX_EN = (1 << 10)
    if ((cap & (1ULL << 8)) && (g_bsp_mtrr_state.def_type & (1ULL << 10))) {

        // 备份掌管 0~512KB 的 1 个寄存器
        g_bsp_mtrr_state.fixed[0] = asm_rdmsr(MSR_MTRRfix64K_00000);

        // 备份掌管 512KB~768KB 的 2 个寄存器
        g_bsp_mtrr_state.fixed[1] = asm_rdmsr(MSR_MTRRfix16K_80000);
        g_bsp_mtrr_state.fixed[2] = asm_rdmsr(MSR_MTRRfix16K_A0000);

        // 备份掌管 768KB~1MB 的 8 个寄存器
        for (int i = 0; i < 8; i++) {
            g_bsp_mtrr_state.fixed[3 + i] = asm_rdmsr(MSR_MTRRfix4K_C0000 + i);
        }
    } else {
        // 如果未开启固定 MTRR，为安全起见将备份区清零
        for(int i = 0; i < 11; i++) {
            g_bsp_mtrr_state.fixed[i] = 0;
        }
    }
}


// ---------------------------------------------------------
// 2. 核心恢复函数 (由 AP 也就是从核 在开启分页前调用)
// ---------------------------------------------------------
void restore_mtrr_state(void) {
    uint64 cr0,flags;

    local_irq_save(&flags);
    // =========================================================
    // 【阶段一：Intel 官方规定的绝对安全停机准备】
    // 此时 AP 应该已经处于 cli (关中断) 状态
    // =========================================================
    // 1. 禁用 Cache (将 CR0 的 CD 位置 1，NW 位置 0)
    cr0 = asm_get_cr0();
    cr0 |= 0x40000000;
    cr0 &= 0xDFFFFFFF;
    asm_set_cr0(cr0);

    // 2. 刷清当前 AP 核的 Cache (极其致命的一步，必须做)
    asm_wbinvd();

    // 3. 刷清 TLB (如果此时 Paging 没开，这步写 CR3 相当于 NOP，但规范要求必须有)
    // 假设你在纯物理地址跑，不写 CR3 也行，但标准的 MTRR 更新流程要求 Flush TLB
    // __asm__ __volatile__ ("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");


    // =========================================================
    // 【阶段二：正式写入 MTRR 数据】
    // =========================================================

    // 4. 挂起 MTRR 引擎
    // (清除 MSR_MTRRdefType 的 ENABLE 位 Bit 11，Intel 规定写 MTRR 前必须先关闭检查)
    asm_wrmsr(MSR_MTRRdefType, g_bsp_mtrr_state.def_type & ~MTRR_DEF_TYPE_ENABLE);

    // 5. 恢复固定范围 MTRR (Fixed-Range MTRRs)
    if (g_bsp_mtrr_state.def_type & MTRR_DEF_TYPE_FIX_EN) {
        asm_wrmsr(MSR_MTRRfix64K_00000, g_bsp_mtrr_state.fixed[0]);
        asm_wrmsr(MSR_MTRRfix16K_80000, g_bsp_mtrr_state.fixed[1]);
        asm_wrmsr(MSR_MTRRfix16K_A0000, g_bsp_mtrr_state.fixed[2]);
        for (int i = 0; i < 8; i++) {
            asm_wrmsr(MSR_MTRRfix4K_C0000 + i, g_bsp_mtrr_state.fixed[3 + i]);
        }
    }

    // 6. 恢复所有的可变范围 MTRR (Variable-Range MTRRs)
    // 注意：这里用的是全局结构体里保存的实际条数 vcnt
    for (uint32 i = 0; i < g_bsp_mtrr_state.vcnt; i++) {
        asm_wrmsr(MSR_MTRRphysBase(i), g_bsp_mtrr_state.var[i].base);
        asm_wrmsr(MSR_MTRRphysMask(i), g_bsp_mtrr_state.var[i].mask);
    }

    // 7. 正式重启 MTRR 引擎！(写入原始带有 ENABLE 位的 def_type)
    asm_wrmsr(MSR_MTRRdefType, g_bsp_mtrr_state.def_type);


    // =========================================================
    // 【阶段三：打扫战场，重新起飞】
    // =========================================================

    // 8. 再次刷清 Cache (确保刚才的 MTRR 变更不会影响已有的缓存管线)
    asm_wbinvd();

    // 9. 重新开启 Cache (恢复 CR0 的 CD 位和 NW 位)
    cr0 = asm_get_cr0();
    cr0 &= 0x9FFFFFFF;
    asm_set_cr0(cr0);

    local_irq_restore(flags);
}