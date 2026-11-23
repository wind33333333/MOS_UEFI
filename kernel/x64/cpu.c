#include "cpu.h"
#include "../init/acpi_init.h"
#include "../init/memblock_init.h"   /* 按你项目实际路径调整 (若已在 include 路径下可改为 "memblock.h") */
#include "msr.h"
#include "printk.h"

/* 全局 CPU 拓扑与状态位图统一收口实例 */
cpu_topo_t cpu_topo = {0};

/* 向上对齐到 64 字节缓存行边界，隔离只读映射表与可写在线位图防止伪共享 */
#define CPU_TOPO_ALIGN64(size)  (((uint64)(size) + 63ULL) & ~63ULL)

/* =========================================================================
 * 内部辅助 1：解析单条 MADT 处理器条目 (统一处理 Type 0 与 Type 9)
 * 返回：TRUE 表示该条目为有效 CPU 槽位 (Enabled 或 Online Capable)，FALSE 表示应忽略
 * ========================================================================= */
static boolean madt_extract_cpu_entry(const madt_header_t *hdr,
                                      uint32 *out_apic_id,
                                      uint32 *out_acpi_uid,
                                      uint8 *out_hw_flags) {
    if (hdr->type == MADT_TYPE_X2APIC) {
        if (hdr->length < sizeof(x2apic_entry_t)) {
            return FALSE;
        }
        const x2apic_entry_t *ent = (const x2apic_entry_t *) hdr;
        if (!MADT_LAPIC_IS_USABLE(ent->flags)) {
            return FALSE;
        }
        *out_apic_id = ent->x2apic_id;
        *out_acpi_uid = ent->processor_id;
        *out_hw_flags = CPU_HW_FLAG_X2APIC
                        | ((ent->flags & MADT_LAPIC_FLAG_ENABLED) ? CPU_HW_FLAG_PRESENT : 0)
                        | ((ent->flags & MADT_LAPIC_FLAG_ONLINE_CAPABLE) ? CPU_HW_FLAG_ONLINE_CAPABLE : 0);
        return TRUE;
    }

    if (hdr->type == MADT_TYPE_LAPIC) {
        if (hdr->length < sizeof(apic_entry_t)) {
            return FALSE;
        }
        const apic_entry_t *ent = (const apic_entry_t *) hdr;
        if (!MADT_LAPIC_IS_USABLE(ent->flags)) {
            return FALSE;
        }
        *out_apic_id = ent->apic_id;
        *out_acpi_uid = ent->processor_id;
        *out_hw_flags = ((ent->flags & MADT_LAPIC_FLAG_ENABLED) ? CPU_HW_FLAG_PRESENT : 0)
                        | ((ent->flags & MADT_LAPIC_FLAG_ONLINE_CAPABLE) ? CPU_HW_FLAG_ONLINE_CAPABLE : 0);
        return TRUE;
    }

    return FALSE;
}

/* =========================================================================
 * 内部辅助 2：判断在当前 MADT 指针之前是否已出现过相同的 ACPI Processor UID
 * 说明：用于 Pass 1 零额外内存开销完成 Type 0 / Type 9 去重计数。
 * ========================================================================= */
static inline boolean madt_uid_seen_before(const madt_header_t *first,
                                           const madt_header_t *limit,
                                           uint32 target_uid) {
    const madt_header_t *scan = first;
    while ((uint64) scan < (uint64) limit) {
        if (scan->length < sizeof(madt_header_t)) {
            break;
        }
        uint32 tmp_apic = 0, tmp_uid = 0;
        uint8 tmp_flags = 0;
        if (madt_extract_cpu_entry(scan, &tmp_apic, &tmp_uid, &tmp_flags)) {
            if (tmp_uid == target_uid) {
                return TRUE;
            }
        }
        scan = (const madt_header_t *) ((uint64) scan + scan->length);
    }
    return FALSE;
}

/* =========================================================================
 * 内部辅助 3：通过 memblock 一次性为 5 个动态数组分配连续内存并按 64B 切分
 * 说明：
 *   - 一次申请全部 5 块区域，避免消耗多个 memblock 保留区间并提升缓存局部性。
 *   - 各区域按 64 字节对齐切分，避免热读的映射表与热写的 online_mask 共享缓存行。
 *   - ⚠️ 若你的 memblock_alloc() 直接返回虚拟地址 void*，去掉 pa_to_va() 转换即可。
 * ========================================================================= */
static uint64 cpu_topo_alloc_dynamic_tables(uint32 nr_possible) {
    uint64 mask_bytes = (uint64) CPUMASK_WORDS_FOR(nr_possible) * sizeof(uint64);

    /* 计算 5 块子区域按 64 字节对齐后的尺寸与总大小 */
    uint64 sz_hw = CPU_TOPO_ALIGN64((uint64)nr_possible * sizeof(cpu_hw_t));
    uint64 sz_lid2aid = CPU_TOPO_ALIGN64((uint64)nr_possible * sizeof(uint32));
    uint64 sz_possible = CPU_TOPO_ALIGN64(mask_bytes);
    uint64 sz_present = CPU_TOPO_ALIGN64(mask_bytes);
    uint64 sz_online = CPU_TOPO_ALIGN64(mask_bytes);
    uint64 total_bytes = sz_hw + sz_lid2aid + sz_possible + sz_present + sz_online;

    /* 从 memblock 申请 64 字节对齐的早期连续物理内存并转换为内核高半区虚拟地址 */
    uint64 pa = memblock_zalloc(total_bytes, 64);
    if (pa == 0) {
        PR_ERROR("[CPU] memblock_alloc failed for topology tables (%lu bytes, nr_possible=%u)!\n",
                 total_bytes, nr_possible);
        while (1) asm_hlt();
    }

    uint8 *base = (uint8 *) pa_to_va(pa);

    /* 依次切分 5 个动态指针 */
    uint64 off = 0;
    cpu_topo.cpu_hw = (cpu_hw_t *) (base + off);
    off += sz_hw;
    cpu_topo.cpu_logicid_to_apicid = (uint32 *) (base + off);
    off += sz_lid2aid;
    cpu_topo.cpu_possible_mask = (uint64 *) (base + off);
    off += sz_possible;
    cpu_topo.cpu_present_mask = (uint64 *) (base + off);
    off += sz_present;
    cpu_topo.cpu_online_mask = (uint64 *) (base + off);

    /* 逻辑 ID -> APIC ID 映射表初始化为无效哨兵值 */
    for (uint32 i = 0; i < nr_possible; i++) {
        cpu_topo.cpu_logicid_to_apicid[i] = CPU_INVALID_ID;
    }

    return total_bytes;
}

/* =========================================================================
 * 核心入口：解析 ACPI MADT，完成动态内存分配、三项核数统计与拓扑建立
 * 流程：
 *   Pass 1 -> 去重扫描 MADT，收敛 nr_possible_cpus (受 CONFIG_NR_CPUS 封顶)
 *   Alloc  -> 通过 memblock 一次性动态分配 5 个拓扑与掩码指针数组
 *   Pass 2 -> 线性填充 cpu_hw[]、合并 Type 0/Type 9 标志位，实时统计：
 *               1) nr_possible_cpus (Enabled + Online Capable)
 *               2) nr_present_cpus  (仅统计 Enabled == 1，重复条目不重复计数)
 *               3) nr_online_cpus   (确认当前运行的 BSP 在线，初始化为 1)
 *             并将 BSP 固定换至 logical_id == 0，同步写入三张动态 cpumask 与映射表
 *   Pass 3 -> 挂载 Type 4 / Type 10 NMI LINT 引脚配置
 * ========================================================================= */
void cpu_topo_init(void) {
    madt_t *madt = acpi_get_table(ACPI_SIG_APIC, 0);
    if (!madt) {
        PR_ERROR("[CPU] No ACPI MADT table found!\n");
        while (1) asm_hlt();
    }

    uint64 madt_end = (uint64) madt + madt->acpi_header.length;
    madt_header_t *madt_first = (madt_header_t *) &madt->entry;
    madt_header_t *cur = NULL;

    /* 读取当前正在执行初始化的 BSP 硬件 APIC ID */
    uint32 bsp_apic_id = (uint32) asm_rdmsr(APIC_ID_MSR);

    /* =====================================================================
     * Pass 1：扫描 MADT 去重统计最大可能核数，确定 memblock 动态分配容量
     * ===================================================================== */
    uint32 cap_possible = 0;

    cur = madt_first;
    while ((uint64) cur + sizeof(madt_header_t) <= madt_end) {
        if (cur->length < sizeof(madt_header_t) || (uint64) cur + cur->length > madt_end) {
            break; /* 防御损坏表项导致死循环或越界读 */
        }

        uint32 hw_apic_id = 0, acpi_uid = 0;
        uint8 hw_flags = 0;
        if (madt_extract_cpu_entry(cur, &hw_apic_id, &acpi_uid, &hw_flags)) {
            if (!madt_uid_seen_before(madt_first, cur, acpi_uid)) {
                cap_possible++;
            }
        }
        cur = (madt_header_t *) ((uint64) cur + cur->length);
    }

    if (cap_possible == 0) {
        PR_ERROR("[CPU] No usable CPU entry (Enabled or Online Capable) in MADT!\n");
        while (1) asm_hlt();
    }

    /* 按精确核数通过 memblock 动态分配 5 个拓扑与位图数组 */
    uint64 alloc_bytes = cpu_topo_alloc_dynamic_tables(cap_possible);

    /* =====================================================================
     * Pass 2：扫描 MADT 填充拓扑，同时精确统计 nr_possible / nr_present / nr_online
     * ===================================================================== */
    uint32 filled = 0;
    uint32 present_cnt = 0;

    cur = madt_first;
    while ((uint64) cur + sizeof(madt_header_t) <= madt_end) {
        if (cur->length < sizeof(madt_header_t) || (uint64) cur + cur->length > madt_end) {
            break;
        }

        uint32 hw_apic_id = 0, acpi_uid = 0;
        uint8 hw_flags = 0;
        if (madt_extract_cpu_entry(cur, &hw_apic_id, &acpi_uid, &hw_flags)) {
            boolean duplicate = FALSE;
            for (uint32 i = 0; i < filled; i++) {
                if (cpu_topo.cpu_hw[i].acpi_proc_id == acpi_uid) {
                    duplicate = TRUE;
                    uint8 old_flags = cpu_topo.cpu_hw[i].flags;

                    /* 同一 UID 同时存在 Type 0 和 Type 9 时，强制采用 Type 9 的 32 位 x2APIC ID */
                    if (hw_flags & CPU_HW_FLAG_X2APIC) {
                        cpu_topo.cpu_hw[i].apic_id = hw_apic_id;
                    }
                    cpu_topo.cpu_hw[i].flags |= hw_flags;

                    /* 仅当合并后首次置位 PRESENT 时才累加，防止重复条目导致 nr_present_cpus 翻倍 */
                    if (!(old_flags & CPU_HW_FLAG_PRESENT) && (hw_flags & CPU_HW_FLAG_PRESENT)) {
                        present_cnt++;
                    }
                    break;
                }
            }

            if (!duplicate && filled < cap_possible) {
                cpu_topo.cpu_hw[filled].apic_id = hw_apic_id;
                cpu_topo.cpu_hw[filled].acpi_proc_id = acpi_uid;
                cpu_topo.cpu_hw[filled].lint_nmi = 0xFF; /* 默认未配置 NMI */
                cpu_topo.cpu_hw[filled].flags = hw_flags;
                if (hw_flags & CPU_HW_FLAG_PRESENT) {
                    present_cnt++;
                }
                filled++;
            }
        }
        cur = (madt_header_t *) ((uint64) cur + cur->length);
    }

    /* 强制保证当前运行的 BSP 固定占据 logical_id == 0 */
    boolean bsp_found = FALSE;
    for (uint32 i = 0; i < filled; i++) {
        if (cpu_topo.cpu_hw[i].apic_id == bsp_apic_id) {
            if (i != 0) {
                cpu_hw_t tmp = cpu_topo.cpu_hw[0];
                cpu_topo.cpu_hw[0] = cpu_topo.cpu_hw[i];
                cpu_topo.cpu_hw[i] = tmp;
            }
            bsp_found = TRUE;
            break;
        }
    }
    if (!bsp_found) {
        PR_INFO("[CPU][WARN] BSP APIC ID %u not matched in MADT, keeping slot 0 as BSP.\n",
                bsp_apic_id);
    }

    /* 兜底校验：正在运行的 BSP 必然属于 present，防止奇葩固件未对 BSP 置 Bit 0 */
    if (!(cpu_topo.cpu_hw[0].flags & CPU_HW_FLAG_PRESENT)) {
        cpu_topo.cpu_hw[0].flags |= CPU_HW_FLAG_PRESENT;
        present_cnt++;
    }

    /* 写入扫描统计出的三项核心数 (启动早期仅 BSP 一颗核心在线，AP 唤醒后由 cpu_mark_online 累加) */
    cpu_topo.nr_possible_cpus = filled;
    cpu_topo.nr_present_cpus = present_cnt;
    cpu_topo.nr_online_cpus = 1;

    /* 统一编排连续 logical_id，填充逻辑映射表与三张动态 cpumask */
    for (uint32 i = 0; i < cpu_topo.nr_possible_cpus; i++) {
        cpu_topo.cpu_hw[i].logical_id = i;
        cpu_topo.cpu_logicid_to_apicid[i] = cpu_topo.cpu_hw[i].apic_id;

        cpumask_set_cpu(cpu_topo.cpu_possible_mask, i);
        if (cpu_topo.cpu_hw[i].flags & CPU_HW_FLAG_PRESENT) {
            cpumask_set_cpu(cpu_topo.cpu_present_mask, i);
        }
    }
    /* 标记 BSP (logical_id == 0) 在线 */
    cpumask_set_cpu(cpu_topo.cpu_online_mask, 0);

    /* =====================================================================
     * Pass 3：挂载 Local APIC / x2APIC NMI LINT 引脚配置
     * ===================================================================== */
    cur = madt_first;
    while ((uint64) cur + sizeof(madt_header_t) <= madt_end) {
        if (cur->length < sizeof(madt_header_t) || (uint64) cur + cur->length > madt_end) {
            break;
        }

        if (cur->type == MADT_TYPE_LAPIC_NMI && cur->length >= sizeof(apic_nmi_entry_t)) {
            apic_nmi_entry_t *ent = (apic_nmi_entry_t *) cur;
            if (ent->apic_id == 0xFF) {
                for (uint32 i = 0; i < cpu_topo.nr_possible_cpus; i++) {
                    cpu_topo.cpu_hw[i].lint_nmi = ent->lint;
                }
            } else {
                for (uint32 i = 0; i < cpu_topo.nr_possible_cpus; i++) {
                    if (cpu_topo.cpu_hw[i].acpi_proc_id == ent->apic_id ||
                        cpu_topo.cpu_hw[i].apic_id == ent->apic_id) {
                        cpu_topo.cpu_hw[i].lint_nmi = ent->lint;
                        break;
                    }
                }
            }
        } else if (cur->type == MADT_TYPE_X2APIC_NMI && cur->length >= sizeof(x2apic_nmi_entry_t)) {
            x2apic_nmi_entry_t *ent = (x2apic_nmi_entry_t *) cur;
            if (ent->x2apic_id == 0xFFFFFFFFU) {
                for (uint32 i = 0; i < cpu_topo.nr_possible_cpus; i++) {
                    cpu_topo.cpu_hw[i].lint_nmi = ent->lint;
                }
            } else {
                for (uint32 i = 0; i < cpu_topo.nr_possible_cpus; i++) {
                    if (cpu_topo.cpu_hw[i].acpi_proc_id == ent->x2apic_id ||
                        cpu_topo.cpu_hw[i].apic_id == ent->x2apic_id) {
                        cpu_topo.cpu_hw[i].lint_nmi = ent->lint;
                        break;
                    }
                }
            }
        }

        cur = (madt_header_t *) ((uint64) cur + cur->length);
    }

    PR_OK("[CPU] Topology ready: possible=%d, present=%d, online=%d (BSP APIC=%d, memblock=%ld B)\n",
          cpu_topo.nr_possible_cpus,
          cpu_topo.nr_present_cpus,
          cpu_topo.nr_online_cpus,
          cpu_topo.cpu_hw[0].apic_id,
          alloc_bytes);
}
