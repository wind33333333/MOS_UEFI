#include "idt.h"
#include "gdt.h"
#include "interrupt.h"
#include "vmm.h"

INIT_DATA idt_ptr_t idt_ptr;

INIT_TEXT void init_idt(void) {
    idt_ptr.limit= 0xFFF;
    idt_ptr.base = (UINT64*)LADDR_TO_HADDR(bitmap_alloc_pages(1));     //分配IDT指针
    bitmap_map_pages(HADDR_TO_LADDR((UINT64)idt_ptr.base),idt_ptr.base,1,PAGE_ROOT_RW);
    mem_set((void*)idt_ptr.base,0,4096);                    //初始化IDT表为0

    //初始化中断向量表为默认中断
    for (int i = 0; i < 256; i++) {
        set_idt_descriptor(i, (UINT64)ignore, IST_1, TYPE_INTRPT);
    }

    //初始化0-20异常
    set_idt_descriptor(0,(UINT64)divide_error,IST_1,TYPE_TRAP);
    set_idt_descriptor(1,(UINT64)debug,IST_1,TYPE_TRAP);
    set_idt_descriptor(2,(UINT64)nmi,IST_1,TYPE_INTRPT);
    set_idt_descriptor(3,(UINT64)int3,IST_1,TYPE_TRAP);
    set_idt_descriptor(4,(UINT64)overflow,IST_1,TYPE_TRAP);
    set_idt_descriptor(5,(UINT64)bounds,IST_1,TYPE_TRAP);
    set_idt_descriptor(6,(UINT64)undefined_opcode,IST_1,TYPE_TRAP);
    set_idt_descriptor(7,(UINT64)dev_not_available,IST_1,TYPE_TRAP);
    set_idt_descriptor(8,(UINT64)double_fault,IST_1,TYPE_TRAP);
    set_idt_descriptor(9,(UINT64)coprocessor_segment_overrun,IST_1,TYPE_TRAP);
    set_idt_descriptor(10,(UINT64)invalid_TSS,IST_1,TYPE_TRAP);
    set_idt_descriptor(11,(UINT64)segment_not_present,IST_1,TYPE_TRAP);
    set_idt_descriptor(12,(UINT64)stack_segment_fault,IST_1,TYPE_TRAP);
    set_idt_descriptor(13,(UINT64)general_protection,IST_1,TYPE_TRAP);
    set_idt_descriptor(14,(UINT64)page_fault,IST_1,TYPE_TRAP);
    //15 Intel reserved. Do not use.
    set_idt_descriptor(16,(UINT64)x87_FPU_error,IST_1,TYPE_TRAP);
    set_idt_descriptor(17,(UINT64)alignment_check,IST_1,TYPE_TRAP);
    set_idt_descriptor(18,(UINT64)machine_check,IST_1,TYPE_TRAP);
    set_idt_descriptor(19,(UINT64)SIMD_exception,IST_1,TYPE_TRAP);
    set_idt_descriptor(20,(UINT64)virtualization_exception,IST_1,TYPE_TRAP);
    //中断
    set_idt_descriptor(0x20,(UINT64)apic_timer,IST_1,TYPE_INTRPT);
    set_idt_descriptor(0x31,(UINT64)keyboard,IST_1,TYPE_INTRPT);
    set_idt_descriptor(0x32,(UINT64)hpet,IST_1,TYPE_INTRPT);

    lidt(&idt_ptr);
}

// 设置中断门描述符
void set_idt_descriptor(UINT32 index, UINT64 function_address, UINT64 ist, UINT64 type){
    // 低 64 位的描述符
    idt_ptr.base[index*2] = ist|type|SEL_CODE64|DPL_0|P|(function_address&0xFFFF)|((function_address >> 16) << 48);
    // 高 64 位的描述符
    idt_ptr.base[index*2+1] = function_address >> 32;
}