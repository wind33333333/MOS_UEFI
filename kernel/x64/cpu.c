#include "cpu.h"

#include "acpi_init.h"
#include "msr.h"
#include "printk.h"

uint64 cpu_possible_mask[CONFIG_NR_CPUS / 64] = {0}; //可能存在的 CPU
uint64 cpu_present_mask[CONFIG_NR_CPUS / 64] = {0}; //已插入的 CPU
uint64 cpu_online_mask[CONFIG_NR_CPUS / 64] = {0}; //完全上线的 CPU

uint32 cpu_logicid_to_apicid[CONFIG_NR_CPUS] = {0xFFFFFFFF}; //cpu逻辑id转apicid

cpu_topo_t cpu_topo = {0};

void cpu_topo_init(void) {
    madt_t *madt = acpi_get_table(ACPI_SIG_APIC, 0);
    if (!madt) {
        PR_ERROR("No MADT!\n");
        while (1) asm_hlt();
    }

    uint64 madt_end = (uint64) madt + madt->acpi_header.length;
    madt_header_t *madt_start;

    // 💡 确保这个函数能正确读取 x2APIC (MSR 0x802) 或 xAPIC (CPUID) 的 ID
    uint32 bsp_apic_id = asm_rdmsr(APIC_ID_MSR); 
    uint32 ap_idx = 0;
    cpu_topo.cpu_count = 0; // 重置为 0，在第二遍真实计算

    // =========================================================
    // 第 1 遍扫描：提取所有的 CPU 核心，并完成去重
    // =========================================================
    madt_start = (madt_header_t *) &madt->entry;
    while ((uint64) madt_start < madt_end) {
        if (madt_start->length == 0) break; // 防御性编程：防止死循环

        uint32 hw_apic_id = 0xFFFFFFFF;
        uint32 acpi_uid = 0xFFFFFFFF;
        boolean is_valid_cpu = FALSE;

        // 提取 Type 9 (x2APIC)
        if (madt_start->type == 9) {
            x2apic_entry_t *ent = (x2apic_entry_t *) madt_start;
            if (ent->flags & 1) { // 检查在线标记
                hw_apic_id = ent->x2apic_id;
                acpi_uid = ent->processor_id; 
                is_valid_cpu = TRUE;
            }
        } 
        // 提取 Type 0 (Local APIC)
        else if (madt_start->type == 0) {
            apic_entry_t *ent = (apic_entry_t *) madt_start;
            if (ent->flags & 1) {
                hw_apic_id = ent->apic_id;
                acpi_uid = ent->processor_id;
                is_valid_cpu = TRUE;
            }
        }

        // 🌟 核心去重逻辑：依赖 ACPI Processor UID
        if (is_valid_cpu) {
            boolean duplicate = FALSE;
            for (uint32 i = 0; i < cpu_topo.cpu_count; i++) {
                if (cpu_topo.cpu_hw[i].acpi_proc_id == acpi_uid) {
                    duplicate = TRUE;
                    // 如果已经注册过（可能是先解析了 Type0），现在遇到了 Type9，
                    // 则必须用 x2APIC_ID 覆盖原来的短 APIC ID
                    if (madt_start->type == 9) {
                        cpu_topo.cpu_hw[i].apic_id = hw_apic_id;
                    }
                    break;
                }
            }

            // 如果是一个全新的核心，分配 logical_id 并保存
            if (!duplicate) {
                uint32 logical_id;
                if (hw_apic_id == bsp_apic_id) {
                    logical_id = 0; 
                } else {
                    logical_id = ++ap_idx;
                }
                cpu_topo.cpu_hw[logical_id].apic_id = hw_apic_id;
                cpu_topo.cpu_hw[logical_id].acpi_proc_id = acpi_uid;
                cpu_topo.cpu_hw[logical_id].logical_id = logical_id;
                // 初始化 NMI 默认值（可选）
                cpu_topo.cpu_hw[logical_id].lint_nmi = 0xFF; 
                
                cpu_topo.cpu_count++;
            }
        }

        // ✅ 指针步进必须在所有的 if 块之外
        madt_start = (madt_header_t *) ((uint64) madt_start + madt_start->length);
    }

    if (cpu_topo.cpu_count == 0) {
        PR_ERROR("No active CPU found!\n");
        while (1) asm_hlt();
    }

    // =========================================================
    // 第 2 遍扫描：挂载 NMI (此时所有 CPU 都已分配完毕)
    // =========================================================
    madt_start = (madt_header_t *) &madt->entry;
    while ((uint64) madt_start < madt_end) {
        
        if (madt_start->type == 4) {
            apic_nmi_entry_t *ent = (apic_nmi_entry_t *) madt_start;
            if (ent->apic_id == 0xFF) {
                for (uint32 i = 0; i < cpu_topo.cpu_count; i++) {
                    cpu_topo.cpu_hw[i].lint_nmi = ent->lint;
                }
            } else {
                for (uint32 i = 0; i < cpu_topo.cpu_count; i++) {
                    if (cpu_topo.cpu_hw[i].apic_id == ent->apic_id) {
                        cpu_topo.cpu_hw[i].lint_nmi = ent->lint;
                        break;
                    }
                }
            }
        } 
        else if (madt_start->type == 10) {
            x2apic_nmi_entry_t *ent = (x2apic_nmi_entry_t *) madt_start;
            if (ent->x2apic_id == 0xFFFFFFFF) {
                for (uint32 i = 0; i < cpu_topo.cpu_count; i++) {
                    cpu_topo.cpu_hw[i].lint_nmi = ent->lint;
                }
            } else {
                for (uint32 i = 0; i < cpu_topo.cpu_count; i++) {
                    if (cpu_topo.cpu_hw[i].apic_id == ent->x2apic_id) {
                        cpu_topo.cpu_hw[i].lint_nmi = ent->lint;
                        break;
                    }
                }
            }
        }

        madt_start = (madt_header_t *) ((uint64) madt_start + madt_start->length);
    }

    //apic id 到 logc id建立映射
    for (uint32 i = 0; i < cpu_topo.cpu_count; i++) {
        cpu_logicid_to_apicid[i] = cpu_topo.cpu_hw[i].apic_id;
    }

}

