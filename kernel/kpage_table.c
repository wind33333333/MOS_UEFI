#include "kpage_table.h"

#include "apic.h"
#include "printk.h"
#include "memblock.h"
#include "vmm.h"

INIT_DATA UINT64* kpml4t_ptr;          //正式内核页表

INIT_TEXT void init_kpage_table(void) {
    kpml4t_ptr = memblock_alloc(PAGE_4K_SIZE, PAGE_4K_SIZE);
    mem_set(kpml4t_ptr, 0, PAGE_4K_SIZE);


    //.init_text-.init_data 可读写执行
    memblock_vmmap(VA_TO_PA(_start_init_text), (void*)VA_TO_PA(_start_init_text), _start_text-_start_init_text >> PAGE_4K_SHIFT, PAGE_ROOT_RWX);
    //.text可读执行
    memblock_vmmap(VA_TO_PA(_start_text), (void*)VA_TO_PA(_start_text), _start_data-_start_text >> PAGE_4K_SHIFT, PAGE_ROOT_RX);
    //.data-.stack可读写
    memblock_vmmap(VA_TO_PA(_start_data), (void*)VA_TO_PA(_start_data), _end_stack-_start_data >> PAGE_4K_SHIFT, PAGE_ROOT_RW);

    //设置正式内核页表
    set_cr3(kpml4t_ptr);
}





