#include "kernel_page_table.h"
#include "buddy_system.h"
#include "memblock.h"
#include "printk.h"
#include "vmm.h"

uint64 *kpml4t_ptr; //正式内核页表

INIT_TEXT void init_kpage_table(void) {
    kpml4t_ptr = (uint64*)memblock_alloc(PAGE_4K_SIZE, PAGE_4K_SIZE);
    asm_mem_set(kpml4t_ptr, 0, PAGE_4K_SIZE);
    //虚拟地址和物理地址512G空间对等映射
    memblock_mmap_range(kpml4t_ptr, 0, (void *) 0, 512 * PAGE_1G_SIZE,PAGE_ROOT_RWX_2M1G,PAGE_1G_SIZE);
    //直接映射区
    memblock_mmap_range(kpml4t_ptr, 0,(void*)DIRECT_MAP_START,
                        memblock.free.region[memblock.free.count - 1].start_pa + memblock.free.region[
                            memblock.free.count - 1].size,PAGE_ROOT_RW_2M1G,PAGE_1G_SIZE);
    //初始化vmemmap区为2M页表,每个page结构64字节，一个page等于4KB,一个2M页刚好等于128MB物理内存。
    for (uint32 i = 0; i <= phy_mem_map.count; i++) {
        uint64 base = align_down(phy_mem_map.region[i].start_pa, 0x8000000);
        uint64 size = align_up(phy_mem_map.region[i].size, 0x8000000);
        uint64 vmemmap_va = (uint64)pa_to_page(base);
        uint32 count = size >> 27;
        while (count--) {
            uint64 pa = memblock_alloc(PAGE_2M_SIZE,PAGE_2M_SIZE);
            asm_mem_set((void *) pa, 0, PAGE_2M_SIZE);
            memblock_mmap(kpml4t_ptr, pa, (void*)vmemmap_va,PAGE_ROOT_RW_2M1G, PAGE_2M_SIZE);
            vmemmap_va += PAGE_2M_SIZE;
        }
    }
    //.init_text-.init_data 可读写执行
    memblock_mmap_range(kpml4t_ptr, (uint64)_start_init_text - KERNEL_START, _start_init_text, _start_text - _start_init_text,
                        PAGE_ROOT_RWX_4K,PAGE_4K_SIZE);
    //.text可读执行
    memblock_mmap_range(kpml4t_ptr, (uint64)_start_text - KERNEL_START, _start_text, _start_data - _start_text,
                        PAGE_ROOT_RX_4K,PAGE_4K_SIZE);
    //.data-.stack可读写
    memblock_mmap_range(kpml4t_ptr, (uint64)_start_data - KERNEL_START, _start_data, _end_stack - _start_data, PAGE_ROOT_RW_4K,
                        PAGE_4K_SIZE);
    //设置正式内核页表
    asm_set_cr3((uint64) kpml4t_ptr);
}
