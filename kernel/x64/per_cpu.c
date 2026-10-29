#include "per_cpu.h"
#include "../init/apic_init.h"
#include "vmm.h"
#include "pmm.h"

extern cpu_apic_info_t cpu_apic_info;
extern vm_space_t kernel_space;

void per_cpu_mem_init() {
    uint32 total_cpu_count = cpu_apic_info.total_cpu_count;
    uint64 per_cpu_va = vm_layout.percpu_vaddr_start;
    for (uint32 i = 0; i < total_cpu_count; i++) {
        uint64 pa = page_to_pa(alloc_pages(4));
        vm_map_range(&kernel_space,per_cpu_va,pa,0x10000,PAGE_KERNEL_DATA_RW);
        per_cpu_va += PERCPU_SIZE;
    }
}