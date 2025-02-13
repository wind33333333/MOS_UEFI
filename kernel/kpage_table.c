#include "kpage_table.h"

#include "apic.h"
#include "printk.h"
#include "memblock.h"
#include "vmm.h"

INIT_DATA UINT64* kpml4t_ptr;          //正式内核页表

INIT_TEXT void init_kpage_table(void) {
    UINT32 pml4e_count,pdpte_count,pde_count,pte_count;
    UINT64 *pml4t,*pdptt,*pdt,*ptt;
    UINT64 phy_addr;

    kpml4t_ptr = memblock_alloc(PAGE_4K_SIZE, PAGE_4K_SIZE);
    mem_set(kpml4t_ptr, 0, PAGE_4K_SIZE);

    //直接映射区
    UINT64 page_count = memblock.memory.region[memblock.memory.count-1].base+memblock.memory.region[memblock.memory.count-1].size >> PAGE_4K_SHIFT;
    pml4e_count = calculate_pml4e_count((void*)DIRECT_MAP_OFFSET,page_count);
    pdpte_count = calculate_pdpte_count((void*)DIRECT_MAP_OFFSET,page_count);
    pml4t = kpml4t_ptr+(DIRECT_MAP_OFFSET>>39&0x1ff);
    phy_addr=0|PAGE_ROOT_RW;
    for (UINT32 i = 0; i < pml4e_count; i++) {
        pdptt = memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE);
        mem_set(pdptt, 0, PAGE_4K_SIZE);
        pml4t[i] = (UINT64)pdptt | PAGE_RW | PAGE_P;
    }

    UINT32 j =0;
    for (UINT32 i = 0; i < pdpte_count; i++) {
        pdptt = pml4t[j]&0x7FFFFFFFF000;
        pdptt[i] = phy_addr;
        phy_addr += 0x40000000;
        if (i == 512) j++;
    }

    //内核映射区
    page_count = _end - _start >> PAGE_4K_SHIFT;
    pml4e_count = calculate_pml4e_count(_start,page_count);
    pdpte_count = calculate_pdpte_count(_start,page_count);
    pde_count = calculate_pde_count(_start,page_count);
    pte_count = page_count;
    pml4t = kpml4t_ptr+(KERNEL_OFFSET>>39&0x1ff);
    for (UINT32 i = 0; i < pml4e_count; i++) {
        pdptt = memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE);
        mem_set(pdptt, 0, PAGE_4K_SIZE);
        pml4t[i] = (UINT64)pdptt | PAGE_RW | PAGE_P;
        for (UINT32 j = 0; j < pdpte_count; j++) {

        }
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





