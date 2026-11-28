#include "../mm/alloc_percpu_internal.h"

/* =========================================================================
 * 内部辅助：前 2 MB PMD 硬件大页模式初始化（静态区 + 前部热点动态区）
 * 返回值：
 *   0  : 成功
 *   1  : 尚未建立任何大页映射即失败（允许安全降级到 4 KB 模式）
 *   2  : 已部分建立 2 MB PMD 映射后中途失败（不可直接覆盖 4 KB，需上层报错）
 * ========================================================================= */
static uint32 pcpu_init_huge_mode(const void *template_vaddr,
                                  uint64 static_size,
                                  uint64 huge_bytes) {
    uint32 huge_pages_per_cpu = (uint32)(huge_bytes >> PCPU_HUGE_SHIFT);
    boolean any_mapped = FALSE;
    uint32 cpu;
    uint32 h;

    for (cpu = 0U; cpu < g_pcpu_nr_cpus; cpu++) {
        uint32 node = pcpu_arch_cpu_to_node(cpu);
        uint64 cpu_va_base = pcpu_cpu_off_to_va(cpu, 0ULL);

        /* 按 2 MB 逐张向本地 NUMA 的 memblock 申请，避免跨物理内存空洞失败 */
        for (h = 0U; h < huge_pages_per_cpu; h++) {
            uint64 phys = pcpu_arch_memblock_alloc_node(PCPU_HUGE_SIZE,
                                                        PCPU_HUGE_SIZE,
                                                        node);
            if (phys == 0ULL) {
                pcpu_arch_log_warn("percpu: 2MB huge alloc failed", (uint64)cpu, (uint64)h);
                return any_mapped ? 2U : 1U;
            }

            /* 经内核直接映射清零整张 2 MB 大页 */
            pcpu_memzero(pcpu_arch_phys_to_virt(phys), PCPU_HUGE_SIZE);

            /* 建立 2 MB PMD 硬件大页映射（省掉该核前 2 MB 所需的 PT 页表） */
            uint64 vaddr = cpu_va_base + (((uint64)h) << PCPU_HUGE_SHIFT);
            if (!pcpu_arch_vmm_map_2m(vaddr, phys)) {
                pcpu_arch_log_warn("percpu: vmm_map_2m failed", vaddr, phys);
                return any_mapped ? 2U : 1U;
            }
            any_mapped = TRUE;
        }

        /* 复制编译期 .data..percpu 静态初始模板到该核起始位置 */
        if (static_size > 0ULL && template_vaddr != NULL) {
            pcpu_memcpy((void *)cpu_va_base, template_vaddr, static_size);
        }
    }

    return 0U;
}

/* =========================================================================
 * 内部辅助：小内存或大页不可用时的 4 KB 按需降级初始化（仅挂载静态段）
 * ========================================================================= */
static boolean pcpu_init_4k_fallback_mode(const void *template_vaddr,
                                          uint64 static_size) {
    uint64 static_aligned = pcpu_align_up_u64(static_size, PCPU_PAGE_SIZE);
    uint32 static_pages   = (uint32)(static_aligned >> PCPU_PAGE_SHIFT);
    uint32 cpu;
    uint32 pg;

    for (cpu = 0U; cpu < g_pcpu_nr_cpus; cpu++) {
        uint32 node = pcpu_arch_cpu_to_node(cpu);
        uint64 cpu_va_base = pcpu_cpu_off_to_va(cpu, 0ULL);

        for (pg = 0U; pg < static_pages; pg++) {
            uint64 phys = pcpu_arch_memblock_alloc_node(PCPU_PAGE_SIZE,
                                                        PCPU_PAGE_SIZE,
                                                        node);
            if (phys == 0ULL) {
                pcpu_arch_log_warn("percpu: 4KB static memblock OOM", (uint64)cpu, (uint64)pg);
                return FALSE;
            }

            pcpu_memzero(pcpu_arch_phys_to_virt(phys), PCPU_PAGE_SIZE);

            uint64 vaddr = cpu_va_base + (((uint64)pg) << PCPU_PAGE_SHIFT);
            if (!pcpu_arch_vmm_map_4k(vaddr, phys)) {
                pcpu_arch_log_warn("percpu: vmm_map_4k failed", vaddr, phys);
                return FALSE;
            }
        }

        if (static_size > 0ULL && template_vaddr != NULL) {
            pcpu_memcpy((void *)cpu_va_base, template_vaddr, static_size);
        }
    }

    return TRUE;
}

/* =========================================================================
 * 对外初始化接口
 * ========================================================================= */
boolean alloc_percpu_early_init(uint64 vaddr_base,
                                const void *template_vaddr,
                                uint64 static_size,
                                uint32 nr_cpus,
                                uint64 total_ram_bytes) {
    uint32 d;

    /* 校验：基址必须 2 MB 对齐，核数有效，静态段不超过窗口一半 */
    if (vaddr_base == 0ULL || (vaddr_base & (PCPU_HUGE_SIZE - 1ULL)) != 0ULL) {
        return FALSE;
    }
    if (nr_cpus == 0U || static_size >= (PERCPU_WINDOW_SIZE >> 1)) {
        return FALSE;
    }

    g_pcpu_vaddr_base = vaddr_base;
    g_pcpu_nr_cpus    = nr_cpus;

    /*
     * 计算动态区起始目录号：
     * 向上对齐到 128 KB 目录边界且恒 >= 1，
     * 保证 alloc_percpu_align() 永远不会返回偏移 0（避免与 NULL 冲突）。
     */
    uint32 first_dyn = (uint32)(pcpu_align_up_u64(static_size, (uint64)PCPU_BMAP_SPAN_SIZE)
                                >> PCPU_BMAP_SPAN_SHIFT);
    if (first_dyn == 0U) {
        first_dyn = 1U;
    }
    g_pcpu_first_dyn_dir = first_dyn;

    /* 判断是否满足前 2 MB 热点 PMD 大页的内存充裕阈值 */
    boolean want_huge = (total_ram_bytes >= (((uint64)nr_cpus) * PCPU_HUGE_RAM_PER_CPU_MIN))
                            ? TRUE
                            : FALSE;
    uint32 huge_ret = 1U; /* 默认按未启用大页处理 */

    if (want_huge) {
        uint64 huge_bytes = (static_size == 0ULL)
                                ? PCPU_HUGE_SIZE
                                : pcpu_align_up_u64(static_size, PCPU_HUGE_SIZE);
        if (huge_bytes < PERCPU_WINDOW_SIZE) {
            huge_ret = pcpu_init_huge_mode(template_vaddr, static_size, huge_bytes);
            if (huge_ret == 0U) {
                g_pcpu_huge_dir_end = (uint32)(huge_bytes >> PCPU_BMAP_SPAN_SHIFT);
            } else if (huge_ret == 2U) {
                /* 已部分写入 2 MB PMD 项后中途 OOM，直接返回致命失败避免页表冲突 */
                return FALSE;
            }
        }
    }

    if (huge_ret != 0U) {
        /* 内存偏小或首个大页即失败时，平滑降级为 4 KB 按需挂载模式 */
        g_pcpu_huge_dir_end = 0U;
        if (!pcpu_init_4k_fallback_mode(template_vaddr, static_size)) {
            return FALSE;
        }
    }

    /*
     * 初始化一级目录项与未激活掩码：
     * 1. [first_dyn_dir, 1024) 初始全部未激活（bmap == NULL，占 0 字节位图内存）；
     * 2. 落入前 2 MB 大页区内的目录将 populated_map 预置为全 1（0xFFFFFFFF），
     *    后续分配零页表开销、释放永不卸载。
     */
    for (d = g_pcpu_first_dyn_dir; d < PCPU_DIR_COUNT; d++) {
        g_pcpu_dir[d].bmap          = NULL;
        g_pcpu_dir[d].populated_map = (d < g_pcpu_huge_dir_end) ? 0xFFFFFFFFU : 0U;
        g_pcpu_dir[d].free_bits     = 0U;
        g_pcpu_dir[d].first_free    = 0U;
        g_pcpu_dir[d].high_water    = 0U;
        g_pcpu_dir[d].contig_hint   = 0U;
        pcpu_mask_set(g_pcpu_uninit_mask, d);
    }

    g_pcpu_early_ready = TRUE;
    return TRUE;
}

void alloc_percpu_dynamic_enable(void) {
    if (g_pcpu_early_ready) {
        g_pcpu_dyn_ready = TRUE;
    }
}