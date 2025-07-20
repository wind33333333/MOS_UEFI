#include "kernel_page_table.h"
#include "buddy_system.h"
#include "memblock.h"
#include "printk.h"
#include "vmm.h"

UINT64 *kpml4t_ptr; //正式内核页表

INIT_TEXT void init_kpage_table(void) {
    kpml4t_ptr = memblock_alloc(PAGE_4K_SIZE, PAGE_4K_SIZE);
    mem_set(kpml4t_ptr, 0, PAGE_4K_SIZE);
    //虚拟地址和物理地址512G空间左对等映射
    memblock_mmap_range(kpml4t_ptr, 0, (void *) 0, 512 * PAGE_1G_SIZE,PAGE_ROOT_RWX_2M1G,PAGE_1G_SIZE);
    //直接映射区
    memblock_mmap_range(kpml4t_ptr, 0,DIRECT_MAP_OFFSET,
                        memblock.memory.region[memblock.memory.count - 1].base + memblock.memory.region[
                            memblock.memory.count - 1].size,PAGE_ROOT_RW_2M1G,PAGE_1G_SIZE);
    //初始化vmemmap区为2M页表,每个page结构64字节，一个page等于4KB,一个2M页刚好等于128MB物理内存。
    for (UINT32 i = 0; i < phy_vmemmap.count; i++) {
        UINT64 vmemmap_va = (UINT64)pa_to_page(phy_vmemmap.region[i].base);
        UINT32 count = phy_vmemmap.region[i].size >> 27;
        while (count--) {
            UINT64 pa = memblock_alloc(PAGE_2M_SIZE,PAGE_2M_SIZE);
            mem_set((void *) pa, 0, PAGE_2M_SIZE);
            memblock_mmap(kpml4t_ptr, pa, vmemmap_va,PAGE_ROOT_RW_2M1G, PAGE_2M_SIZE);
            vmemmap_va += PAGE_2M_SIZE;
        }
    }
    //.init_text-.init_data 可读写执行
    memblock_mmap_range(kpml4t_ptr, _start_init_text - KERNEL_OFFSET, _start_init_text, _start_text - _start_init_text,
                        PAGE_ROOT_RWX_4K,PAGE_4K_SIZE);
    //.text可读执行
    memblock_mmap_range(kpml4t_ptr, _start_text - KERNEL_OFFSET, _start_text, _start_data - _start_text,
                        PAGE_ROOT_RX_4K,PAGE_4K_SIZE);
    //.data-.stack可读写
    memblock_mmap_range(kpml4t_ptr, _start_data - KERNEL_OFFSET, _start_data, _end_stack - _start_data, PAGE_ROOT_RW_4K,
                        PAGE_4K_SIZE);
    //设置正式内核页表
    set_cr3((UINT64) kpml4t_ptr);
}
