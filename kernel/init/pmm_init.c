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
// 伙伴系统 NUMA 感知初始化 (极致降维 O(1) 自动分拣版)
// =========================================================================
void buddy_system_init(void) {
    vmemmap_init(); // 映射 struct page 全局数组 (绑定 NUMA 本地内存)

    // 1. 定位 page_map 的虚拟基址
    buddy_system.vmemmap = (page_t *) vm_layout.vmemmap_start;

    // 2. 遍历初始化所有 Node 的所有阶空闲链表
    for (uint8 n = 0; n < CONFIG_NUMA_NODES; n++) {
        for (uint64 i = 0; i <= MAX_ORDER; i++) {
            list_head_init(&buddy_system.zones[n].free_area[i].list);
            buddy_system.zones[n].free_area[i].count = 0;
        }
    }

    // 3. 遍历已经被 SRAT 完美盖过戳的 memblock 资产簿
    for (uint32 i = 0; i < memblock.memory.count; i++) {
        uint64 start_pa = memblock.memory.region[i].base;
        uint64 count = memblock.memory.region[i].size >> 12;
        // 🌟 提取 Memblock 在早期阶段千辛万苦洗出来的物理 Node 归属
        uint8 region_nid = memblock.memory.region[i].nid;

        // 容错：如果有漏网之鱼，强行划归 Node 0 避免游离
        if (region_nid == NUMA_NO_NODE) region_nid = 0;

        uint64 start_idx = pa_to_page(start_pa) - buddy_system.vmemmap;
        uint64 end_idx = start_idx + count;

        while (start_idx < end_idx) {
            uint32 order = MAX_ORDER;

            // 智能嗅探当前游标能满足的最高阶数
            while (order > 0) {
                if ((start_idx & ((1ULL << order) - 1)) == 0 && (start_idx + (1ULL << order) <= end_idx)) {
                    break;
                }
                order--;
            }

            page_t *p = buddy_system.vmemmap + start_idx;

            // 彻底清洗该大块的数据
            p->order = order;
            p->flags = 0;
            p->refcount = 0;

            // 🌟 核心注入：在交接给伙伴系统前，把 Node ID 烙印在 Page 的标志位里！
            // 这样稍后的 free_pages 就会通过这个标志，自动把它挂接到 buddy_system.zones[region_nid] 里！
            p->flags |= ((uint64) region_nid << PG_NID_SHIFT);

            // 顺带把尾部连续页也打上同样的印记 (用于后续分离合并时的识别)
            for (uint32 j = 1; j < (1ULL << order); j++) {
                (p + j)->flags = 0;
                set_page_nid(p + j, region_nid);
            }

            // 扔进回收引擎，实现全自动分拣归位！
            free_pages(p);

            start_idx += (1ULL << order);
        }
    }

    // 4. 将支持 NUMA 的伙伴系统正式绑定到虚拟内存 VMM 回调接口，接管天下
    kernel_space.ops.alloc_pages = alloc_pages;
    kernel_space.ops.free_pages = free_pages;
    kernel_space.ops.page_to_phys = page_to_pa;
    kernel_space.ops.phys_to_page = pa_to_page;
    kernel_space.ops.phys_to_virt = pa_to_va;
    kernel_space.ops.virt_to_phys = va_to_pa;

    PR_OK("NUMA-Aware Buddy System init success.\n");
}
