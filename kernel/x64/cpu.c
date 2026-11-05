#include "cpu.h"

#include "acpi_init.h"
#include "msr.h"
#include "printk.h"

uint64 cpu_possible_mask[CONFIG_NR_CPUS / 64] = {0}; //可能存在的 CPU
uint64 cpu_present_mask[CONFIG_NR_CPUS / 64] = {0}; //已插入的 CPU
uint64 cpu_online_mask[CONFIG_NR_CPUS / 64] = {0}; //完全上线的 CPU

uint32 cpu_logicid_to_apicid[CONFIG_NR_CPUS] = {0xFFFFFFFF}; //cpu逻辑id转apicid

cpu_topo_t cpu_topo = {0};

cpu_info_t cpu_info = {
    .ccb_ptr = NULL, //运行时动态分配
    .run_cpu_count = 1, //启动时只有bsp核心在运行，后面每个ap核心启动后自动修改
    .total_cpu_count = 1 //后面解析acpi表后修改cpu核心总数
};


void cpu_topo_init(void) {
    madt_t *madt = acpi_get_table(ACPI_SIG_APIC, 0);
    if (!madt) {
        PR_ERROR("No MADT!\n");
        while (1) {
            asm_hlt();
        };
    }

    madt_header_t *madt_start = (madt_header_t *) &madt->entry;
    uint64 madt_end = (uint64) madt + madt->acpi_header.length;

    // =========================================================
    // 第一遍扫描：纯计数 (Count) - 【全面统计 3 大巨头】
    // =========================================================
    uint32 count_apic = 0, count_x2apic = 0;

    while ((uint64) madt_start < madt_end) {
        switch (madt_start->type) {
            case 0: if (((apic_entry_t *) madt_start)->flags & 1) count_apic++;
                break;
            case 9: if (((x2apic_entry_t *) madt_start)->flags & 1) count_x2apic++;
                break;
        }
        madt_start = (madt_header_t *) ((uint64) madt_start + madt_start->length);
    }
    // 🌟 核心决策：如果存在 x2APIC，就用 x2APIC 的数量，否则回退到传统 APIC
    cpu_topo.cpu_count = (count_x2apic > 0) ? count_x2apic : count_apic;
    if (cpu_topo.cpu_count == 0) {
        PR_ERROR("No active CPU found!\n");
        while (1) {
            asm_hlt();
        };
    }

    // 💡 必须让 BSP 永远霸占 logical_id = 0 的宝座！
    uint32 ap_idx = 0;
    uint32 bsp_apic_id = asm_rdmsr(APIC_ID_MSR);

    // =========================================================
    // 第二遍扫描：提取数据并填充 (Populate)
    // =========================================================
    madt_start = (madt_header_t *) &madt->entry;

    if (count_x2apic == 0) {
        while ((uint64) madt_start < madt_end) {
            if (madt_start->type == 0) {
                //传统的 Local APIC (Type 0)
                apic_entry_t *lapic_entry = (apic_entry_t *) madt_start;
                if (lapic_entry->flags & 1) {
                    uint32 logical_id;
                    if (lapic_entry->apic_id == bsp_apic_id) {
                        logical_id = 0;
                    } else {
                        logical_id = ++ap_idx;
                    }
                    cpu_topo.cpu_hw[logical_id].apic_id = lapic_entry->apic_id;
                    cpu_topo.cpu_hw[logical_id].acpi_proc_id = lapic_entry->processor_id;
                    cpu_topo.cpu_hw[logical_id].logical_id = logical_id;
                } else if (madt_start->type == 4) {
                    //apic nmi引脚
                    apic_nmi_entry_t *apic_nmi_entry = (apic_nmi_entry_t *) madt_start;
                    // 如果 ID 是 0xFF，说明这是“全服广播”
                    if (apic_nmi_entry->apic_id == 0xFF) {
                        for (uint32 i = 0; i < cpu_topo.cpu_count; i++) {
                            cpu_topo.cpu_hw[i].lint_nmi = apic_nmi_entry->lint;
                        }
                    } else {
                        // 针对特定 CPU，去数组里找它
                        for (uint32 i = 0; i < cpu_topo.cpu_count; i++) {
                            if (cpu_topo.cpu_hw[i].apic_id == apic_nmi_entry->apic_id) {
                                cpu_topo.cpu_hw[i].lint_nmi = apic_nmi_entry->lint;
                                break;
                            }
                        }
                    }
                }
                madt_start = (madt_header_t *) ((uint64) madt_start + madt_start->length);
            }
        }
    } else {
        while ((uint64) madt_start < madt_end) {
            if (madt_start->type == 9) {
                //X2APIC ID
                x2apic_entry_t *x2apic_entry = (x2apic_entry_t *) madt_start;
                if (x2apic_entry->flags & 1) {
                    uint32 logical_id;
                    if (x2apic_entry->x2apic_id == bsp_apic_id) {
                        logical_id = 0;
                    } else {
                        logical_id = ++ap_idx;
                    }
                    cpu_topo.cpu_hw[logical_id].apic_id = x2apic_entry->x2apic_id;
                    cpu_topo.cpu_hw[logical_id].acpi_proc_id = x2apic_entry->processor_id;
                    cpu_topo.cpu_hw[logical_id].logical_id = logical_id;
                }
            } else if (madt_start->type == 10) {
                //X2APIC不可屏蔽中断
                x2apic_nmi_entry_t *x2apic_nmi_entry = (x2apic_nmi_entry_t *) madt_start;
                // X2APIC 的广播 ID 是 0xFFFFFFFF
                if (x2apic_nmi_entry->x2apic_id == 0xFFFFFFFF) {
                    for (uint32 i = 0; i < cpu_topo.cpu_count; i++) {
                        cpu_topo.cpu_hw[i].lint_nmi = x2apic_nmi_entry->lint;
                    }
                } else {
                    for (uint32 i = 0; i < cpu_topo.cpu_count; i++) {
                        if (cpu_topo.cpu_hw[i].apic_id == x2apic_nmi_entry->x2apic_id) {
                            cpu_topo.cpu_hw[i].lint_nmi = x2apic_nmi_entry->lint;
                            break;
                        }
                    }
                }
            }

            madt_start = (madt_header_t *) ((uint64) madt_start + madt_start->length);
        }
    }
}
