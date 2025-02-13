#include "kpage_table.h"

#include "apic.h"
#include "printk.h"
#include "memblock.h"
#include "vmm.h"

INIT_DATA UINT64 *kpml4t_ptr; //正式内核页表

INIT_TEXT void init_kpage_table(void) {
    kpml4t_ptr = memblock_alloc(PAGE_4K_SIZE, PAGE_4K_SIZE);
    mem_set(kpml4t_ptr, 0, PAGE_4K_SIZE);

    //直接映射区
    memblock_vmmap_1g(kpml4t_ptr, 0,DIRECT_MAP_OFFSET,
                        memblock.memory.region[memblock.memory.count - 1].base + memblock.memory.region[
                            memblock.memory.count - 1].size,PAGE_ROOT_RW_2M1G);
    //.init_text-.init_data 可读写执行
    memblock_vmmap_4k(kpml4t_ptr,_start_init_text-KERNEL_OFFSET, _start_init_text, _start_text - _start_init_text,
                    PAGE_ROOT_RWX);
    //.text可读执行
    memblock_vmmap_4k(kpml4t_ptr,_start_text-KERNEL_OFFSET, _start_text, _start_data - _start_text, PAGE_ROOT_RX);
    //.data-.stack可读写
    memblock_vmmap_4k(kpml4t_ptr,_start_data-KERNEL_OFFSET, _start_data, _end_stack - _start_data, PAGE_ROOT_RW);

    //memblock_vmmap_4k(PA_TO_VA(kpml4t_ptr),0,0xFFFF807FFFFFF000,2,PAGE_ROOT_RWX);

    //设置正式内核页表
    set_cr3(kpml4t_ptr);
}
