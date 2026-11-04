#include "../mm/alloc_percpu.h"
#include "apic_init.h"
#include "../x64/cpu.h"
#include "list.h"
#include "moslib.h"
#include "pmm.h"
#include "vmm.h"

extern list_head_t chunk_list;
extern pcpu_chunk_t first_chunk;
extern pcpu_block_t pcpu_cpu_block;
extern cpu_apic_info_t cpu_apic_info;
extern uint64 pcpu_chunk_bitmap[4];
extern pcpu_block_t free_block;
extern vm_space_t kernel_space;

void alloc_percpu_init() {
    uint32 total_cpu_count = cpu_apic_info.total_cpu_count;
    cpu_info.total_cpu_count = total_cpu_count;
    uint64 per_cpu_va = vm_layout.percpu_vaddr_start;

    for (uint32 i = 0; i < total_cpu_count; i++) {
        uint64 pa = page_to_pa(alloc_pages(4));
        vm_map_range(&kernel_space, per_cpu_va, pa, PCPU_CHUNK_SIZE, PAGE_KERNEL_DATA_RW);
        per_cpu_va += PERCPU_WINDOW_SIZE;
    }

    list_head_init(&chunk_list);
    list_head_init(&first_chunk.block_list);

    // 🌟 创世 Chunk 永远占据 offset 0，所以必须在位图中把第 0 位锁死 (置 1)
    pcpu_chunk_bitmap[0] = 1ULL;

    // 手动创建第一个 chunk
    first_chunk.start_off = 0;
    first_chunk.end_off = PCPU_CHUNK_SIZE;
    first_chunk.free_bytes = PCPU_CHUNK_SIZE - sizeof(cpu_control_block_t);
    list_add_tail(&first_chunk.node, &chunk_list);

    // 手动创建 per_cpu 的 block
    pcpu_cpu_block.is_free = FALSE;
    pcpu_cpu_block.is_dynamic = FALSE;
    pcpu_cpu_block.offset = 0;
    pcpu_cpu_block.size = sizeof(cpu_control_block_t);
    list_add_tail(&pcpu_cpu_block.node, &first_chunk.block_list);

    // 手动创建第一个空闲 block
    free_block.is_free = TRUE;
    free_block.is_dynamic = FALSE;
    free_block.offset = sizeof(cpu_control_block_t);
    free_block.size = PCPU_CHUNK_SIZE - sizeof(cpu_control_block_t);
    list_add_tail(&free_block.node, &first_chunk.block_list);
}