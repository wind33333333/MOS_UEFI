#include "pmm.h"
#include "pmm_init.h"
#include "memblock_init.h"
#include "slub.h"
#include "printk.h"

extern vm_space_t kernel_space;

// 伙伴系统最大阶对应的物理块字节大小 (例如 MAX_ORDER=10 -> 4MB)
#define BUDDY_MAX_BLOCK_SIZE ((uint64)PAGE_4K_SIZE << MAX_ORDER)
#define BUDDY_ALIGN_DOWN(x)  ((x) & ~(BUDDY_MAX_BLOCK_SIZE - 1))
#define BUDDY_ALIGN_UP(x)    (((x) + BUDDY_MAX_BLOCK_SIZE - 1) & ~(BUDDY_MAX_BLOCK_SIZE - 1))

typedef struct {
    uint64 va_start;
    uint64 va_end;
    uint8 nid;
} vmemmap_range_t;

// 放在静态区 (若有 __initdata 宏可加上，启动完毕随 .init_data 一起释放，不占内核栈)
static vmemmap_range_t vmemmap_plan[MAX_MEMBLOCK_REGIONS];

// =========================================================================
// 阶段一：纯数学区间消毒 (无任何硬件/页表副作用)
// 产出：严格升序、零重叠、同节点已极致合并的虚拟地址规划表
// =========================================================================
static inline uint32 build_vmemmap_plan(void) {
    uint32 raw_count = 0;

    // 1. 从 phys_map 投影并按 MAX_ORDER 块大小向外对齐
    for (uint32 i = 0; i < memblock.phys_map.count; i++) {
        uint64 p_start = memblock.phys_map.region[i].base;
        uint64 p_size = memblock.phys_map.region[i].size;
        if (!p_size) continue;

        uint64 aligned_p_start = BUDDY_ALIGN_DOWN(p_start);
        uint64 aligned_p_end = BUDDY_ALIGN_UP(p_start + p_size);

        uint64 va_start = PAGE_4K_ALIGN_DOWN((uint64)pa_to_page(aligned_p_start));
        uint64 va_end = PAGE_4K_ALIGN((uint64)pa_to_page(aligned_p_end));

        vmemmap_plan[raw_count].va_start = va_start;
        vmemmap_plan[raw_count].va_end = va_end;
        vmemmap_plan[raw_count].nid = memblock.phys_map.region[i].nid;
        raw_count++;
    }

    if (raw_count == 0) return 0;

    // 2. 插入排序：保证严格按 va_start 升序 (防御异常 UEFI 内存图)
    for (uint32 i = 1; i < raw_count; i++) {
        vmemmap_range_t key = vmemmap_plan[i];
        int32 j = (int32) i - 1;
        while (j >= 0 && vmemmap_plan[j].va_start > key.va_start) {
            vmemmap_plan[j + 1] = vmemmap_plan[j];
            j--;
        }
        vmemmap_plan[j + 1] = key;
    }

    // 3. 原地消毒：同 Node 合并愈合，跨 Node 截断消重
    uint32 clean_count = 1;
    for (uint32 i = 1; i < raw_count; i++) {
        vmemmap_range_t *prev = &vmemmap_plan[clean_count - 1];
        vmemmap_range_t curr = vmemmap_plan[i];

        if (curr.nid == prev->nid && curr.va_start <= prev->va_end) {
            // 同 NUMA 节点且重叠/接壤：直接合并，最大化 2MB/1GB 大页连续性
            if (curr.va_end > prev->va_end) {
                prev->va_end = curr.va_end;
            }
        } else {
            // 跨 NUMA 节点发生边缘重叠：向右截断起点，消除重叠
            if (curr.va_start < prev->va_end) {
                curr.va_start = prev->va_end;
            }
            // 若截断后完全被前一段覆盖，则直接丢弃
            if (curr.va_start >= curr.va_end) {
                continue;
            }
            vmemmap_plan[clean_count++] = curr;
        }
    }

    return clean_count;
}

// =========================================================================
// 阶段二：纯粹的流式物理映射 (无需任何 vm_is_mapped 探测)
// =========================================================================
static inline void mount_vmemmap_clean(uint64 va_start, uint64 va_end, uint8 target_nid) {
    uint64 va_curr = va_start;

    while (va_curr < va_end) {
        uint64 remaining = va_end - va_curr;
        uint64 step_size;
        uint64 map_flags = PAGE_KERNEL_DATA_RW;
        uint64 vmemmap_pa = 0;

        // 1档：1GB 巨页 (TB 级内存，单调性保证该 1GB 槽位绝对干净)
        if ((va_curr & PAGE_1G_OFFSET_MASK) == 0 && remaining >= PAGE_1G_SIZE) {
            vmemmap_pa = memblock_alloc_node(PAGE_1G_SIZE, PAGE_1G_SIZE, target_nid);
            if (vmemmap_pa) {
                step_size = PAGE_1G_SIZE;
                map_flags |= SW_FLAG_MAX_1G;
                goto do_map;
            }
        }

        // 2档：2MB 大页 (单调性保证该 2MB 槽位绝对干净)
        if ((va_curr & PAGE_2M_OFFSET_MASK) == 0 && remaining >= PAGE_2M_SIZE) {
            vmemmap_pa = memblock_alloc_node(PAGE_2M_SIZE, PAGE_2M_SIZE, target_nid);
            if (vmemmap_pa) {
                step_size = PAGE_2M_SIZE;
                map_flags |= SW_FLAG_MAX_2M;
                goto do_map;
            }
        }

        // 3档：4KB 兜底 (首尾未对齐碎片区)
        step_size = PAGE_4K_SIZE;
        vmemmap_pa = memblock_alloc_node(PAGE_4K_SIZE, PAGE_4K_SIZE, target_nid);
        if (!vmemmap_pa) {
            PR_ERROR("OOM allocating vmemmap (4KB)! Node %d\n", target_nid);
            while (1) asm_hlt();
        }

    do_map:
        // 清洗元数据内存
        asm_mem_set(pa_to_va(vmemmap_pa), 0, step_size);
        vm_map_range(&kernel_space, va_curr, vmemmap_pa, step_size, map_flags);
        va_curr += step_size;
    }
}

// =========================================================================
// 静态拓扑预计算：在 buddy_system_init 中调用一次即可
// 为每个 NUMA Zone 提前构建好按 SLIT 物理距离升序排列的降级路由表
// =========================================================================
void buddy_build_zonelists(void) {
    // 兼容无 SRAT/SLIT 表的 UMA 机器：至少保证 Node 0 激活
    uint8 total_nodes = numa_info.numa_nodes_count;
    if (total_nodes == 0) {
        total_nodes = 1;
    } else if (total_nodes > CONFIG_NUMA_NODES) {
        total_nodes = CONFIG_NUMA_NODES;
    }
    buddy_system.active_nodes = total_nodes;

    for (uint8 target_nid = 0; target_nid < total_nodes; target_nid++) {
        numa_zone_t *zone = &buddy_system.zones[target_nid];
        uint8 count = 0;

        // 第 0 位永远锁死为本地节点 (最高优先级)
        zone->fallback_nids[count++] = target_nid;

        // 收集所有远端节点
        uint8 sort_start = count;
        for (uint8 i = 0; i < total_nodes; i++) {
            if (i != target_nid) {
                zone->fallback_nids[count++] = i;
            }
        }

        // 启动期一次性按 SLIT 物理距离表排序，热路径从此 O(1) 零开销
        for (uint8 i = sort_start; i < count; i++) {
            for (uint8 j = i + 1; j < count; j++) {
                uint8 node_a = zone->fallback_nids[i];
                uint8 node_b = zone->fallback_nids[j];

                uint8 dist_a = numa_info.numa_distance[target_nid][node_a];
                uint8 dist_b = numa_info.numa_distance[target_nid][node_b];

                if (dist_b < dist_a || (dist_b == dist_a && node_b < node_a)) {
                    zone->fallback_nids[i] = node_b;
                    zone->fallback_nids[j] = node_a;
                }
            }
        }

        zone->fallback_count = count;
    }
}

// =========================================================================
// 启动期专用交接接口 (供 buddy_system_init 调用，直接入库 Buddy)
// =========================================================================
void buddy_init_free_block(page_t *page, uint32 order, uint8 nid) {
    if (!page || order > MAX_ORDER) return;
    if (nid >= buddy_system.active_nodes) nid = 0;

    uint64 num_pages = 1ULL << order;
    for (uint64 i = 0; i < num_pages; i++) {
        page[i].refcount = 0;
        page[i].compound_head = 0;
        page[i].order = 0;
        page_reset_flags_keep_nid(&page[i], nid);
    }

    numa_zone_t *zone = &buddy_system.zones[nid];
    uint64 irq_flags;
    spin_lock_irqsave(&zone->lock,&irq_flags);
    __buddy_free_locked(zone, page, order, nid);
    spin_unlock_irqrestore(&zone->lock, irq_flags);
}

// =========================================================================
// 总入口：先消毒，再映射
// =========================================================================
static void vmemmap_init(void) {
    uint32 plan_count = build_vmemmap_plan();

    for (uint32 i = 0; i < plan_count; i++) {
        mount_vmemmap_clean(vmemmap_plan[i].va_start,
                            vmemmap_plan[i].va_end,
                            vmemmap_plan[i].nid);
    }
}

// =========================================================================
// 伙伴系统 NUMA 感知初始化 (锁初始化 + 路由表预计算 + 专用交接引擎)
// =========================================================================
void buddy_system_init(void) {
    // 1. 映射 struct page 全局数组 (先做区间消毒，再按 NUMA 本地内存流式挂载)
    vmemmap_init();

    // 2. 定位 vmemmap 虚拟基址
    buddy_system.vmemmap = (page_t *)vm_layout.vmemmap_start;

    // 3. 初始化所有 NUMA Zone 的自旋锁、空闲计数与各阶链表
    for (uint8 n = 0; n < CONFIG_NUMA_NODES; n++) {
        buddy_system.zones[n].lock = 0;
        buddy_system.zones[n].free_pages_count = 0;
        buddy_system.zones[n].fallback_count   = 0;

        for (uint32 i = 0; i <= MAX_ORDER; i++) {
            list_head_init(&buddy_system.zones[n].free_area[i].list);
            buddy_system.zones[n].free_area[i].count = 0;
        }
    }

    // 4. 🌟 一次性预计算全系统 NUMA 降级路由表，并确定 active_nodes
    //    必须在遍历 memblock.memory 之前调用，确保后续 nid 校验与入库使用正确的节点数
    buddy_build_zonelists();

    // 5. 遍历经 SRAT 盖章后的 memblock.memory 资产簿，将空闲物理页移交伙伴系统
    for (uint32 i = 0; i < memblock.memory.count; i++) {
        uint64 start_pa = memblock.memory.region[i].base;
        uint64 count    = memblock.memory.region[i].size >> 12;
        if (count == 0) continue;

        uint8 region_nid = memblock.memory.region[i].nid;

        // 容错：未匹配 SRAT 或超出激活范围的节点统一安全回落到 Node 0
        if (region_nid == NUMA_NO_NODE || region_nid >= buddy_system.active_nodes) {
            region_nid = 0;
        }

        uint64 start_idx = (uint64)(pa_to_page(start_pa) - buddy_system.vmemmap);
        uint64 end_idx   = start_idx + count;

        while (start_idx < end_idx) {
            uint32 order = MAX_ORDER;

            // 嗅探当前游标能满足的最大 2^order 对齐阶数
            while (order > 0) {
                if ((start_idx & ((1ULL << order) - 1)) == 0 &&
                    (start_idx + (1ULL << order) <= end_idx)) {
                    break;
                }
                order--;
            }

            page_t *p = buddy_system.vmemmap + start_idx;

            // 🌟 使用启动期专用交接接口：统一清洗块内所有页元数据 (flags/nid/refcount/compound_head)
            //    并直接进入持锁融合引擎，避免触发运行期 free_pages 的 refcount 校验报错
            buddy_init_free_block(p, order, region_nid);

            start_idx += (1ULL << order);
        }
    }

    // 6. 将 NUMA 伙伴系统正式绑定到 VMM 物理页回调接口，完成控制权交接
    //    (注：此后严禁再调用 memblock_alloc*，如需强约束可在此调用 memblock_mark_retired())
    kernel_space.ops.alloc_pages  = alloc_pages;
    kernel_space.ops.free_pages   = free_pages;
    kernel_space.ops.page_to_phys = page_to_pa;
    kernel_space.ops.phys_to_page = pa_to_page;
    kernel_space.ops.phys_to_virt = pa_to_va;
    kernel_space.ops.virt_to_phys = va_to_pa;

    // 7. 打印各激活 NUMA 节点接管到的空闲内存统计，便于启动期核验
    uint64 total_free_pages = 0;
    for (uint8 n = 0; n < buddy_system.active_nodes; n++) {
        uint64 node_pages = buddy_system.zones[n].free_pages_count;
        total_free_pages += node_pages;
        PR_INFO("  -> NUMA Node %d Free Memory: %ld MB (%ld pages)\n",
                n, (node_pages << 12) / 1024 / 1024, node_pages);
    }

    PR_OK("NUMA-Aware Buddy System init success. Active Nodes: %d, Total Free: %ld MB\n",
          buddy_system.active_nodes, (total_free_pages << 12) / 1024 / 1024);
}