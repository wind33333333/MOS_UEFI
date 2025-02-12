#include "kpage_table.h"

#include "apic.h"
#include "printk.h"
#include "memblock.h"
#include "vmm.h"

INIT_DATA UINT64* kpml4t_ptr;          //正式内核页表

INIT_TEXT void init_kpage_table(void) {
    UINT32 count;
    UINT64 *pml4t,*pdptt,*pdt,*ptt;
    UINT64 phy_addr;

    kpml4t_ptr = memblock_alloc(PAGE_4K_SIZE, PAGE_4K_SIZE);
    mem_set(kpml4t_ptr, 0, PAGE_4K_SIZE);

    //直接映射区
    count = memblock.memory.region[memblock.memory.count-1].base+memblock.memory.region[memblock.memory.count-1].size+0x7FFFFFFFFF >> 39;
    pml4t = kpml4t_ptr+(DIRECT_MAP_OFFSET>>39&0x1ff);
    phy_addr=0|PAGE_ROOT_RW;
    for (UINT32 i = 0; i < count; i++) {
        pdptt = memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE);
        mem_set(pdptt, 0, PAGE_4K_SIZE);
        pml4t[i] = (UINT64)pdptt | PAGE_RW | PAGE_P;
        for (UINT32 j = 0; j < 512; j++) {
            pdptt[j] = phy_addr;
            phy_addr += PAGE_4K_SIZE;
        }
    }

    //内核映射区
    count = _end - _start + 0x7FFFFFFFFF >> 39;
    pml4t = kpml4t_ptr+(KERNEL_OFFSET>>39&0x1ff);
    for (UINT32 i = 0; i < count; i++) {
        pdptt = memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE);
        pml4t[i] = (UINT64)pdptt | PAGE_RW | PAGE_P;
    }


    //.init_text-.init_data 可读写执行
    memblock_vmmap(VA_TO_PA(_start_init_text), (void*)VA_TO_PA(_start_init_text), _start_text-_start_init_text >> PAGE_4K_SHIFT, PAGE_ROOT_RWX);
    //.text可读执行
    memblock_vmmap(VA_TO_PA(_start_text), (void*)VA_TO_PA(_start_text), _start_data-_start_text >> PAGE_4K_SHIFT, PAGE_ROOT_RX);
    //.data-.stack可读写
    memblock_vmmap(VA_TO_PA(_start_data), (void*)VA_TO_PA(_start_data), _end_stack-_start_data >> PAGE_4K_SHIFT, PAGE_ROOT_RW);

    //设置正式内核页表
    set_cr3(kpml4t_ptr);
}





