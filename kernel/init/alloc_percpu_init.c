#include "../mm/alloc_percpu.h"
#include "../x64/cpu.h"
#include "../x64/numa_node.h"
#include "memblock_init.h"
#include "moslib.h"
#include "printk.h"
#include "vmm.h"

extern vm_space_t kernel_space;

/* =========================================================================
 * Per-CPU 子系统早期启动初始化 (运行于 memblock 阶段、Buddy/SLUB 启动之前)
 * 职责：
 *   1. 按 nr_possible_cpus 与 256KB 核间步长规划创世 Chunk 0 虚拟空间；
 *   2. 仅通过 memblock_alloc_node 预映射覆盖 CCB 及早期启动所需的头部页 (默认 64KB)，
 *      Chunk 0 剩余 192KB 保留未映射，待运行期按页按需挂载，彻底避免开机物理内存浪费；
 *   3. 初始化双位图、pinned_map 固定页保护掩码，并永久保留偏移 0 的 CCB 区域。
 * ========================================================================= */
void alloc_percpu_init(void) {
    uint32 nr_cpus = cpu_topo.nr_possible_cpus;
    if (nr_cpus == 0) {
        PR_ERROR("[PERCPU] nr_possible_cpus is 0! Call cpu_topo_init() first.\n");
        while (1) asm_hlt();
    }

    /* 计算覆盖 cpu_control_block_t 所需的最少 4KB 页数，并与早期缓冲下限取大值 */
    uint32 ccb_bytes    = (uint32)sizeof(cpu_control_block_t);
    uint32 ccb_pages    = (ccb_bytes + PCPU_PAGE_SIZE - 1U) >> PCPU_PAGE_SHIFT;
    uint32 premap_pages = (ccb_pages > PCPU_EARLY_PREMAP_PAGES) ? ccb_pages : PCPU_EARLY_PREMAP_PAGES;

    if (premap_pages > PCPU_CHUNK_PAGES) {
        PR_ERROR("[PERCPU] CCB size (%d B) exceeds PCPU_CHUNK_SIZE (%ld B)!\n",
                 ccb_bytes, PCPU_CHUNK_SIZE);
        while (1) asm_hlt();
    }

    uint64 premap_bytes = (uint64)premap_pages * PCPU_PAGE_SIZE;
    uint64 base_va      = vm_layout.percpu_vaddr_start;

    /* 仅为每个逻辑核心预映射前 premap_bytes (本地 NUMA 节点分配)，
     * 每核槽位按 PERCPU_WINDOW_SIZE (256KB) 步进，后半段虚拟空间留待按页按需挂载。
     */
    for (uint32 i = 0; i < nr_cpus; i++) {
        uint8  node_lid = cpu_lid_to_numa_lid(i);
        uint64 pa       = memblock_alloc_node(premap_bytes, PCPU_PAGE_SIZE, node_lid);
        if (pa == 0ULL) {
            PR_ERROR("[PERCPU] memblock_alloc_node failed for CPU#%d (node=%d, bytes=%ld)!\n",
                     i, node_lid, premap_bytes);
            while (1) asm_hlt();
        }

        uint64 cpu_va = base_va + ((uint64)i * PERCPU_WINDOW_SIZE);
        vm_map_range(&kernel_space, cpu_va, pa, premap_bytes, PAGE_KERNEL_DATA_RW);
    }

    /* 初始化创世 Chunk 0 双位图，并锁定头部 CCB 空间与 memblock 预映射页 */
    pcpu_first_chunk_setup(base_va, ccb_bytes, premap_pages);

    PR_OK("[PERCPU] Infinite Dual-Bitmap allocator ready: %d CPUs, Chunk=%ldKB, early-mapped=%ldKB (CCB=%dB)\n",
          nr_cpus,
          PCPU_CHUNK_SIZE / 1024ULL,
          premap_bytes / 1024ULL,
          ccb_bytes);
}