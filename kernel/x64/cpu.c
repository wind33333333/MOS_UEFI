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
 * 内部辅助 3：判定 MADT 处理器条目是否应计入 nr_possible_cpus
 * 规则：
 *   1) 符合 ACPI 6.3+ 规范：Enabled (Bit 0) 或 Online Capable (Bit 1) 置位 -> 有效；
 *   2) 兼容 QEMU 等虚拟化平台：MADT 版本 < 5 (早于 ACPI 6.3) 时，未插入的热插拔槽位
 *      flags 固定为 0，仅在虚拟化 Guest 环境下放行并视作可热插拔预留槽位；
 *   3) 物理真机上 flags == 0 一律视为 BIOS 屏蔽核或空插槽占位项，严格丢弃。
 * ========================================================================= */
static inline boolean madt_cpu_is_possible(uint32 flags, uint8 madt_rev, boolean is_guest) {
    if (MADT_LAPIC_IS_USABLE(flags)) {
        return TRUE;
    }
    if (is_guest && madt_rev < 5) {
        return TRUE;
    }
    return FALSE;
}

/* =========================================================================
 * 内部辅助 4：解析单条 MADT 处理器条目 (统一处理 Type 0 与 Type 9)
 * 说明：同时过滤非法广播/占位 APIC ID (Type 0 的 0xFF、Type 9 的 0xFFFFFFFF)。
 * ========================================================================= */
static boolean madt_extract_cpu_entry(const madt_header_t *hdr,
                                      uint8   madt_rev,
                                      boolean is_guest,
                                      uint32 *out_apic_id,
                                      uint32 *out_acpi_uid,
                                      uint8  *out_hw_flags) {
    if (hdr->type == MADT_TYPE_X2APIC) {
        if (hdr->length < sizeof(x2apic_entry_t)) {
            return FALSE;
        }
        const x2apic_entry_t *ent = (const x2apic_entry_t *)hdr;

        /* 拦截非法占位/广播 x2APIC ID */
        if (ent->x2apic_id == 0xFFFFFFFFU) {
            return FALSE;
        }
        if (!madt_cpu_is_possible(ent->flags, madt_rev, is_guest)) {
            return FALSE;
        }

        uint8 hw_flags = CPU_HW_FLAG_X2APIC;
        if (ent->flags & MADT_LAPIC_FLAG_ENABLED) {
            hw_flags |= CPU_HW_FLAG_PRESENT;
        }
        /* 若显式置位 Bit 1，或经由 Guest 旧版 MADT 兼容规则放行的离线槽位，均标为可热插拔 */
        if ((ent->flags & MADT_LAPIC_FLAG_ONLINE_CAPABLE) || !(ent->flags & MADT_LAPIC_FLAG_ENABLED)) {
            hw_flags |= CPU_HW_FLAG_ONLINE_CAPABLE;
        }

        *out_apic_id  = ent->x2apic_id;
        *out_acpi_uid = ent->processor_id;
        *out_hw_flags = hw_flags;
        return TRUE;
    }

    if (hdr->type == MADT_TYPE_LAPIC) {
        if (hdr->length < sizeof(apic_entry_t)) {
            return FALSE;
        }
        const apic_entry_t *ent = (const apic_entry_t *)hdr;

        /* 拦截传统 xAPIC 广播/无效占位 ID (0xFF) */
        if (ent->apic_id == 0xFFU) {
            return FALSE;
        }
        if (!madt_cpu_is_possible(ent->flags, madt_rev, is_guest)) {
            return FALSE;
        }

        uint8 hw_flags = 0;
        if (ent->flags & MADT_LAPIC_FLAG_ENABLED) {
            hw_flags |= CPU_HW_FLAG_PRESENT;
        }
        if ((ent->flags & MADT_LAPIC_FLAG_ONLINE_CAPABLE) || !(ent->flags & MADT_LAPIC_FLAG_ENABLED)) {
            hw_flags |= CPU_HW_FLAG_ONLINE_CAPABLE;
        }

        *out_apic_id  = ent->apic_id;
        *out_acpi_uid = ent->processor_id;
        *out_hw_flags = hw_flags;
        return TRUE;
    }

    return FALSE;
}

/* =========================================================================
 * 内部辅助 5：判断在当前 MADT 指针之前是否已出现过相同的 ACPI Processor UID
 * 说明：用于 Pass 1 零额外内存开销地完成 Type 0 / Type 9 去重计数。
 * ========================================================================= */
static boolean madt_uid_seen_before(const madt_header_t *first,
                                    const madt_header_t *limit,
                                    uint8   madt_rev,
                                    boolean is_guest,
                                    uint32  target_uid) {
    const madt_header_t *scan = first;
    while ((uint64)scan + sizeof(madt_header_t) <= (uint64)limit) {
        if (scan->length < sizeof(madt_header_t) || (uint64)scan + scan->length > (uint64)limit) {
            break;
        }
        uint32 tmp_apic = 0, tmp_uid = 0;
        uint8  tmp_flags = 0;
        if (madt_extract_cpu_entry(scan, madt_rev, is_guest, &tmp_apic, &tmp_uid, &tmp_flags)) {
            if (tmp_uid == target_uid) {
                return TRUE;
            }
        }
        scan = (const madt_header_t *)((uint64)scan + scan->length);
    }
    return FALSE;
}

/* =========================================================================
 * 内部辅助 6：通过 memblock 一次性为 5 个动态数组分配连续内存并按 64B 切分
 * 布局：[ cpu_hw ] [ cpu_logicid_to_apicid ] [ possible_mask ] [ present_mask ] [ online_mask ]
 * 优势：单次申请节省 memblock 保留区槽位；各数组 64B 缓存行对齐，防止多核伪共享。
 * ⚠️ 接入注意：若你的 memblock_alloc() 直接返回虚拟地址，请去掉 pa_to_va() 转换。
 * ========================================================================= */
static uint64 cpu_topo_alloc_dynamic_tables(uint32 nr_possible) {
    uint64 mask_bytes = (uint64)CPUMASK_WORDS_FOR(nr_possible) * sizeof(uint64);

    uint64 sz_hw        = CPU_TOPO_ALIGN64((uint64)nr_possible * sizeof(cpu_hw_t));
    uint64 sz_lid2aid   = CPU_TOPO_ALIGN64((uint64)nr_possible * sizeof(uint32));
    uint64 sz_possible  = CPU_TOPO_ALIGN64(mask_bytes);
    uint64 sz_present   = CPU_TOPO_ALIGN64(mask_bytes);
    uint64 sz_online    = CPU_TOPO_ALIGN64(mask_bytes);
    uint64 total_bytes  = sz_hw + sz_lid2aid + sz_possible + sz_present + sz_online;

    uint64 pa = memblock_zalloc(total_bytes, 64);
    if (pa == 0) {
        PR_ERROR("[CPU] memblock_alloc failed for topology tables (%ld bytes, nr_possible=%d)!\n",
                 total_bytes, nr_possible);
        while (1) asm_hlt();
    }

    uint8 *base = (uint8 *)pa_to_va(pa);

    /* 按 64 字节边界依次切分 5 个动态指针 */
    uint64 off = 0;
    cpu_topo.cpu_hw                = (cpu_hw_t *)(base + off); off += sz_hw;
    cpu_topo.cpu_logicid_to_apicid = (uint32   *)(base + off); off += sz_lid2aid;
    cpu_topo.cpu_possible_mask     = (uint64   *)(base + off); off += sz_possible;
    cpu_topo.cpu_present_mask      = (uint64   *)(base + off); off += sz_present;
    cpu_topo.cpu_online_mask       = (uint64   *)(base + off);

    /* 将逻辑核号 -> APIC ID 查表数组初始化为无效哨兵 */
    for (uint32 i = 0; i < nr_possible; i++) {
        cpu_topo.cpu_logicid_to_apicid[i] = CPU_INVALID_ID;
    }

    return total_bytes;
}

/* =========================================================================
 * 核心入口：解析 ACPI MADT，动态分配内存、统计三项核心数并建立完整拓扑
 * 流程：
 *   Pass 1 -> 去重扫描 MADT，统计无上限的精确可能核数 cap_possible
 *   Alloc  -> 通过 memblock 一次性动态分配 5 个拓扑与位图数组
 *   Pass 2 -> 线性填充 cpu_hw[]、合并 Type 0 / Type 9 标志位，实时统计：
 *               1) nr_possible_cpus (全部可用与热插拔预留槽位)
 *               2) nr_present_cpus  (仅统计 Enabled == 1，去重不重复累加)
 *               3) nr_online_cpus   (启动阶段初始化为 1，对应当前运行的 BSP)
 *             并将 BSP 固定交换至 logical_id == 0，同步写入三张动态位图与映射表
 *   Pass 3 -> 绑定 Type 4 / Type 10 NMI LINT 引脚配置
 * ========================================================================= */
void cpu_topo_init(void) {
    madt_t *madt = acpi_get_table(ACPI_SIG_APIC, 0);
    if (!madt) {
        PR_ERROR("[CPU] No ACPI MADT table found!\n");
        while (1) asm_hlt();
    }

    uint64         madt_end   = (uint64)madt + madt->acpi_header.length;
    uint8          madt_rev   = madt->acpi_header.revision;
    boolean        is_guest   = cpu_in_hypervisor();
    madt_header_t *madt_first = (madt_header_t *)&madt->entry;
    madt_header_t *cur        = NULL;

    /* 读取当前正在执行初始化的 BSP 硬件 APIC ID */
    uint32 bsp_apic_id = asm_rdmsr(APIC_ID_MSR);

    /* =====================================================================
     * Pass 1：扫描 MADT 去重统计最大可能核数 (无编译期硬上限截断)
     * ===================================================================== */
    uint32 cap_possible = 0;

    cur = madt_first;
    while ((uint64)cur + sizeof(madt_header_t) <= madt_end) {
        if (cur->length < sizeof(madt_header_t) || (uint64)cur + cur->length > madt_end) {
            break; /* 防御损坏表项导致死循环或越界读 */
        }

        uint32 hw_apic_id = 0, acpi_uid = 0;
        uint8  hw_flags   = 0;
        if (madt_extract_cpu_entry(cur, madt_rev, is_guest, &hw_apic_id, &acpi_uid, &hw_flags)) {
            if (!madt_uid_seen_before(madt_first, cur, madt_rev, is_guest, acpi_uid)) {
                cap_possible++;
            }
        }
        cur = (madt_header_t *)((uint64)cur + cur->length);
    }

    if (cap_possible == 0) {
        PR_ERROR("[CPU] No usable CPU entry found in MADT (rev=%d, guest=%d)!\n",
                 madt_rev, is_guest);
        while (1) asm_hlt();
    }

    /* 按精确核数通过 memblock 动态分配 5 个拓扑与掩码数组 */
    uint64 alloc_bytes = cpu_topo_alloc_dynamic_tables(cap_possible);

    /* =====================================================================
     * Pass 2：线性紧凑填充并去重，同时精确统计 nr_possible / nr_present / nr_online
     * ===================================================================== */
    uint32 filled      = 0;
    uint32 present_cnt = 0;

    cur = madt_first;
    while ((uint64)cur + sizeof(madt_header_t) <= madt_end) {
        if (cur->length < sizeof(madt_header_t) || (uint64)cur + cur->length > madt_end) {
            break;
        }

        uint32 hw_apic_id = 0, acpi_uid = 0;
        uint8  hw_flags   = 0;
        if (madt_extract_cpu_entry(cur, madt_rev, is_guest, &hw_apic_id, &acpi_uid, &hw_flags)) {
            boolean duplicate = FALSE;
            for (uint32 i = 0; i < filled; i++) {
                if (cpu_topo.cpu_hw[i].acpi_proc_id == acpi_uid) {
                    duplicate = TRUE;
                    uint8 old_flags = cpu_topo.cpu_hw[i].flags;

                    /* 同一 UID 同时存在 Type 0 与 Type 9 时，强制采用 Type 9 的 32 位 x2APIC ID */
                    if (hw_flags & CPU_HW_FLAG_X2APIC) {
                        cpu_topo.cpu_hw[i].apic_id = hw_apic_id;
                    }
                    cpu_topo.cpu_hw[i].flags |= hw_flags;

                    /* 仅在首次置位 PRESENT 时累加，防止 Type 0/9 合并时重复计数 */
                    if (!(old_flags & CPU_HW_FLAG_PRESENT) && (hw_flags & CPU_HW_FLAG_PRESENT)) {
                        present_cnt++;
                    }
                    break;
                }
            }

            if (!duplicate && filled < cap_possible) {
                cpu_topo.cpu_hw[filled].apic_id      = hw_apic_id;
                cpu_topo.cpu_hw[filled].acpi_proc_id = acpi_uid;
                cpu_topo.cpu_hw[filled].lint_nmi     = 0xFF; /* 默认未配置 NMI */
                cpu_topo.cpu_hw[filled].flags        = hw_flags;
                if (hw_flags & CPU_HW_FLAG_PRESENT) {
                    present_cnt++;
                }
                filled++;
            }
        }
        cur = (madt_header_t *)((uint64)cur + cur->length);
    }

    /* 强制保证当前运行的 BSP 固定占据 logical_id == 0 */
    boolean bsp_found = FALSE;
    for (uint32 i = 0; i < filled; i++) {
        if (cpu_topo.cpu_hw[i].apic_id == bsp_apic_id) {
            if (i != 0) {
                cpu_hw_t tmp       = cpu_topo.cpu_hw[0];
                cpu_topo.cpu_hw[0] = cpu_topo.cpu_hw[i];
                cpu_topo.cpu_hw[i] = tmp;
            }
            bsp_found = TRUE;
            break;
        }
    }
    if (!bsp_found) {
        PR_INFO("[CPU][WARN] BSP APIC ID %d not matched in MADT, keeping slot 0 as BSP.\n",
                bsp_apic_id);
    }

    /* 兜底校验：正在运行的 BSP 必然处于 present 状态 */
    if (!(cpu_topo.cpu_hw[0].flags & CPU_HW_FLAG_PRESENT)) {
        cpu_topo.cpu_hw[0].flags |= CPU_HW_FLAG_PRESENT;
        present_cnt++;
    }

    /* 锁定扫描统计出的三项核数 (当前仅 BSP 一颗核在线，AP 唤醒后由 cpu_mark_online 递增) */
    cpu_topo.nr_possible_cpus = filled;
    cpu_topo.nr_present_cpus  = present_cnt;
    cpu_topo.nr_online_cpus   = 1;

    /* 编排连续 logical_id，同步写入查表数组与三张动态 cpumask */
    for (uint32 i = 0; i < cpu_topo.nr_possible_cpus; i++) {
        cpu_topo.cpu_hw[i].logical_id     = i;
        cpu_topo.cpu_logicid_to_apicid[i] = cpu_topo.cpu_hw[i].apic_id;

        cpumask_set_cpu(cpu_topo.cpu_possible_mask, i);
        if (cpu_topo.cpu_hw[i].flags & CPU_HW_FLAG_PRESENT) {
            cpumask_set_cpu(cpu_topo.cpu_present_mask, i);
        }
    }
    /* 标记 BSP (logical_id == 0) 在线 */
    cpumask_set_cpu(cpu_topo.cpu_online_mask, 0);

    /* =====================================================================
     * Pass 3：挂载 Local APIC (Type 4) 与 x2APIC (Type 10) NMI LINT 引脚配置
     * ===================================================================== */
    cur = madt_first;
    while ((uint64)cur + sizeof(madt_header_t) <= madt_end) {
        if (cur->length < sizeof(madt_header_t) || (uint64)cur + cur->length > madt_end) {
            break;
        }

        if (cur->type == MADT_TYPE_LAPIC_NMI && cur->length >= sizeof(apic_nmi_entry_t)) {
            apic_nmi_entry_t *ent = (apic_nmi_entry_t *)cur;
            if (ent->apic_id == 0xFFU) {
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
            x2apic_nmi_entry_t *ent = (x2apic_nmi_entry_t *)cur;
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

        cur = (madt_header_t *)((uint64)cur + cur->length);
    }

    PR_OK("[CPU] Topology ready: possible=%d, present=%d, online=%d | MADT rev=%d, guest=%s | BSP APIC=%d, memblock=%ld B\n",
          cpu_topo.nr_possible_cpus,
          cpu_topo.nr_present_cpus,
          cpu_topo.nr_online_cpus,
          madt_rev,
          is_guest ? "YES" : "NO",
          cpu_topo.cpu_hw[0].apic_id,
          alloc_bytes);
}