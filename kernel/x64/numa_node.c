#include "numa_node.h"
#include "../init/acpi_init.h"
#include "cpu.h"
#include "../init/memblock_init.h"
#include "printk.h"

numa_info_t numa_info = {0};


// =========================================================================
// 核心映射辅助函数：物理号 -> 逻辑号 转换器
// =========================================================================

/**
 * @brief 将稀疏的物理 Proximity Domain 转换为紧凑连续的 逻辑 Node ID (0, 1, 2...)
 */
static uint8 get_or_create_logical_node(uint32 phys_domain) {
    // 1. 查找是否已经注册过该物理 Domain
    uint8 numa_nodes_count = numa_info.numa_nodes_count;
    uint32* phys_domain_table = numa_info.phys_domain_table;
    for (uint8 i = 0; i < numa_nodes_count; i++) {
        if (phys_domain_table[i] == phys_domain) {
            return i; // 返回已存在的逻辑 ID
        }
    }

    // 2. 如果没找到，说明发现了一个新的 NUMA 节点，分配新 ID
    if (numa_nodes_count >= CONFIG_NUMA_NODES) {
        PR_ERROR("NUMA nodes exceed CONFIG_NUMA_NODES limit!\n");
        while (1) asm_hlt(); // 生产环境请走 Panic 流程
    }

    uint8 logical_node = numa_nodes_count;
    phys_domain_table[logical_node] = phys_domain;
    numa_info.numa_nodes_count++;

    return logical_node;
}

/**
 * @brief 仅用于 SLIT 解析时，将逻辑 Node ID 反向查回物理 Domain
 */
static uint32 logical_node_to_phys_domain(uint8 logical_node) {
    if (logical_node >= numa_info.numa_nodes_count) return 0xFFFFFFFF;
    return numa_info.phys_domain_table[logical_node];
}

// =========================================================================
// 1. 解析 SRAT (系统资源亲和性表)
// =========================================================================
static inline void acpi_parse_srat(void) {
    srat_t *srat_ptr = acpi_get_table(ACPI_SIG_SRAT, 0);
    if (!srat_ptr) return;

    acpi_header_t *header = (acpi_header_t *) srat_ptr;
    PR_INFO("[ACPI] Parsing SRAT...\n");

    // 初始化映射数组
    for (int i = 0; i < CONFIG_NR_CPUS; i++) {
        numa_info.cpu_lid_to_numa_map[i] = 0xFF;
    }

    // SRAT 头部固定偏移 48 字节 (Header 36 + Reserved 12)
    uint8 *cursor = (uint8 *) srat_ptr + 48;
    uint8 *end = (uint8 *) srat_ptr + header->length;

    while (cursor < end) {
        srat_entry_header_t *entry = (srat_entry_header_t *) cursor;
        if (entry->length == 0) break; // 防御性拦截死循环

        if (entry->type == 0) {
            // Type 0: Local APIC
            srat_apic_t *stra_apic = (srat_apic_t *) entry;
            if (stra_apic->flags & 0x01) {
                // 检查 Enabled 标志
                // ⚠️ 拼装被历史遗留问题切碎的 32-bit 物理 Domain ID
                uint32 phys_domain = stra_apic->proximity_domain_lo |
                                     (stra_apic->proximity_domain_hi[0] << 8) |
                                     (stra_apic->proximity_domain_hi[1] << 16) |
                                     (stra_apic->proximity_domain_hi[2] << 24);

                // 获取两个逻辑 ID (完全脱离物理环境)
                uint32 logical_cpu = cpu_aid_to_lid(stra_apic->apic_id);
                uint8 logical_node = get_or_create_logical_node(phys_domain);

                if (logical_cpu != 0xFFFFFFFF) {
                    numa_info.cpu_lid_to_numa_map[logical_cpu] = logical_node;
                }
            }
        } else if (entry->type == 1) {
            // 在 SRAT Type 1 解析逻辑中：
            srat_memory_t *mem = (srat_memory_t *) entry;
            // 只要 Enabled(Bit 0) 或 HotPluggable(Bit 1) 有一个为真，就要记录
            if (mem->flags & 0x01 || mem->flags & 0x02) {
                uint64 base = mem->base_addr;
                uint64 size = mem->length;
                uint32 node = get_or_create_logical_node(mem->proximity_domain);

                // 1. 记录到“领土备忘录”中，供未来热插拔查询
                if (numa_info.numa_mem_count < CONFIG_NUMA_MEM_REGIONS) {
                    numa_mem_region_t* numa_mem_region = &numa_info.numa_mem_region[numa_info.numa_mem_count];
                    numa_mem_region->base = base;
                    numa_mem_region->size = size;
                    numa_mem_region->node_id = node;
                    numa_mem_region->is_hotpluggable = (mem->flags & 0x02) ? TRUE : FALSE;
                    numa_info.numa_mem_count++;
                }

            }
        } else if (entry->type == 2) {
            // Type 2: x2APIC
            srat_x2apic_t *srat_x2apic = (srat_x2apic_t *) entry;
            if (srat_x2apic->flags & 0x01) {
                uint32 phys_domain = srat_x2apic->proximity_domain; // x2APIC 很干净，直接是 32 位

                uint32 logical_cpu = cpu_aid_to_lid(srat_x2apic->x2apic_id);
                uint8 logical_node = get_or_create_logical_node(phys_domain);

                if (logical_cpu != 0xFFFFFFFF) {
                    numa_info.cpu_lid_to_numa_map[logical_cpu] = logical_node;
                }
            }
        }

        cursor += entry->length;
    }

    PR_INFO("[ACPI] SRAT parsing complete. Detected %d Logical NUMA Nodes.\n", numa_info.numa_nodes_count);
}

// =========================================================================
// 2. 解析 SLIT (系统局部性信息表)
// =========================================================================
static inline void acpi_parse_slit() {
    slit_t *slit_ptr = acpi_get_table(ACPI_SIG_SLIT, 0);
    if (!slit_ptr) {
        PR_INFO("[ACPI] No SLIT found. Assuming flat NUMA distances.\n");
        return;
    }

    slit_t *slit = (slit_t *) slit_ptr;
    uint64 phys_localities = slit->localities; // 物理维度的矩阵大小，可能存在巨量空洞

    // 1. 初始化我们自己的逻辑二维数组
    // 对角线为 10 (本地访问基准距离)，其余初始化为不可达 (255)
    for (int i = 0; i < CONFIG_NUMA_NODES; i++) {
        for (int j = 0; j < CONFIG_NUMA_NODES; j++) {
            numa_info.numa_distance[i][j] = (i == j) ? 10 : 255;
        }
    }

    // 2. 遍历有效逻辑 Node (只提取需要的数据，丢弃物理矩阵的空洞)
    uint32 numa_nodes_count = numa_info.numa_nodes_count;
    for (uint8 log_i = 0; log_i < numa_nodes_count; log_i++) {
        for (uint8 log_j = 0; log_j < numa_nodes_count; log_j++) {
            // 将逻辑坐标还原为物理坐标，去固件给的矩阵里查数据
            uint32 phys_i = logical_node_to_phys_domain(log_i);
            uint32 phys_j = logical_node_to_phys_domain(log_j);

            // 防御越界：如果物理域编号超出了 SLIT 给的维度，跳过
            if (phys_i >= phys_localities || phys_j >= phys_localities) {
                continue;
            }

            // 从一维柔性数组中，使用物理坐标计算偏移并提取距离数据
            uint8 dist = slit->entries[phys_i * phys_localities + phys_j];

            // 存入紧凑的逻辑矩阵中 (供调度器和 SLUB 读取)
            numa_info.numa_distance[log_i][log_j] = dist;

            // *Verification:* 观察此处打印，应只显示连续紧凑的逻辑节点距离
            PR_INFO("  [SLIT] Logical Node %d -> Node %d : Distance %d\n", log_i, log_j, dist);
        }
    }

    PR_INFO("[ACPI] SLIT matrix loaded and compacted successfully.\n");
}

// 在 numa_node.c 的上方，添加一个简单的辅助函数用于排序
static inline void sort_numa_mem_regions(void) {
    // 采用简单的冒泡排序（因为记录数 CONFIG_NUMA_MEM_REGIONS 很小，通常不到几十个）
    for (uint32 i = 0; i < numa_info.numa_mem_count; i++) {
        for (uint32 j = i + 1; j < numa_info.numa_mem_count; j++) {
            if (numa_info.numa_mem_region[i].base > numa_info.numa_mem_region[j].base) {
                numa_mem_region_t temp = numa_info.numa_mem_region[i];
                numa_info.numa_mem_region[i] = numa_info.numa_mem_region[j];
                numa_info.numa_mem_region[j] = temp;
            }
        }
    }
}

// =========================================================================
// 3. 清洗与消毒 SRAT 收集到的内存区间 (numa_mem_region)
// =========================================================================
static inline void sanitize_numa_mem_regions(void) {
    if (numa_info.numa_mem_count == 0) return;

    // 1. 第一步：按 base 地址进行升序排序
    sort_numa_mem_regions();

    // 2. 第二步：遍历并处理重叠 (Overlap Resolution)
    uint32 valid_count = 0;

    // 我们用一个紧凑指针（valid_count）在原数组上直接重构清洗后的数据
    for (uint32 i = 0; i < numa_info.numa_mem_count; i++) {
        numa_mem_region_t *curr = &numa_info.numa_mem_region[i];

        // 剔除无效的空区间
        if (curr->size == 0) continue;

        uint64 curr_end = curr->base + curr->size;

        if (valid_count > 0) {
            numa_mem_region_t *prev = &numa_info.numa_mem_region[valid_count - 1];
            uint64 prev_end = prev->base + prev->size;

            // 检查当前区间是否与上一个有效区间重叠
            if (curr->base < prev_end) {
                PR_WARN("[NUMA] Overlap detected: [%#lx - %#lx] Node %d overlaps with [%#lx - %#lx] Node %d\n",
                        prev->base, prev_end, prev->node_id,
                        curr->base, curr_end, curr->node_id);

                if (prev->node_id == curr->node_id && prev->is_hotpluggable == curr->is_hotpluggable) {
                    // 情况 A：同一节点且属性相同，直接把它们融合成一个更大的区间！
                    if (curr_end > prev_end) {
                        prev->size = curr_end - prev->base;
                    }
                    // 当前节点已被融合，无需加入 valid 队列，直接 continue
                    continue;
                } else {
                    // 情况 B：不同节点发生物理地址重叠（典型的 BIOS 脏数据）
                    // 业界标准做法：截断后面的区间，尊重排在前面的区间
                    if (curr_end > prev_end) {
                        curr->base = prev_end;
                        curr->size = curr_end - curr->base;
                        PR_WARN("       Fixed by truncating current region to [%#lx - %#lx]\n", curr->base, curr_end);
                    } else {
                        // 当前区间被上一个区间完全包裹了，直接丢弃！
                        PR_WARN("       Fixed by dropping the fully overlapped region.\n");
                        continue;
                    }
                }
            }
        }

        // 如果经过重叠处理后，区间依然有效，则写入有效队列中
        if (valid_count != i) {
            numa_info.numa_mem_region[valid_count] = *curr;
        }
        valid_count++;
    }

    // 3. 更新系统真实的、干净的 region 数量
    numa_info.numa_mem_count = valid_count;

    // 打印清洗后的干净结果以供验证
    PR_INFO("[NUMA] Sanitization complete. Valid regions: %d\n", valid_count);
    for (uint32 i = 0; i < valid_count; i++) {
        PR_INFO("  Region %d: Base %#lx, Size %#lx, Node %d (Hotpluggable: %d)\n",
                 i, numa_info.numa_mem_region[i].base, numa_info.numa_mem_region[i].size,
                 numa_info.numa_mem_region[i].node_id, numa_info.numa_mem_region[i].is_hotpluggable);
    }
}

void numa_node_init(void) {

    acpi_parse_srat();

    acpi_parse_slit();

    sanitize_numa_mem_regions();

    // 1. 遍历界碑表，对 Memblock 的所有资产和债务逐一盖章
    for (uint32 i = 0; i < numa_info.numa_mem_count; i++) {
        numa_mem_region_t *reg = &numa_info.numa_mem_region[i];

        // 资产簿 (空闲内存) 盖章
        memblock_set_node_by_intersection(&memblock.memory, reg->base, reg->size, reg->node_id);

        // 债务簿 (保留内存) 盖章，确保未来的 memblock_free 能够精准寻祖
        memblock_set_node_by_intersection(&memblock.reserved, reg->base, reg->size, reg->node_id);
    }

}