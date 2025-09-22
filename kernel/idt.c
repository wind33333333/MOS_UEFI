#include "idt.h"
#include "gdt.h"
#include "interrupt.h"
#include "slub.h"
#include "vmalloc.h"

INIT_DATA idt_ptr_t idt_ptr;

// 设置中断门描述符
void set_idt_descriptor(uint32 index, uint64 function_address, uint64 ist, uint64 type){
    // 低 64 位的描述符
    idt_ptr.base[index*2] = ist|type|SEL_CODE64|DPL_0|P|(function_address&0xFFFF)|((function_address >> 16) << 48);
    // 高 64 位的描述符
    idt_ptr.base[index*2+1] = function_address >> 32;
}

INIT_TEXT void init_idt(void) {
    idt_ptr.limit= 4096-1;
    idt_ptr.base = vmalloc(4096);     //分配IDT指针

    //初始化中断向量表为默认中断
    for (int i = 0; i < 256; i++) {
        set_idt_descriptor(i, (uint64)ignore, IST_1, TYPE_INTRPT);
    }

    //初始化0-20异常
    set_idt_descriptor(0,(uint64)divide_error,IST_1,TYPE_TRAP);
    set_idt_descriptor(1,(uint64)debug,IST_1,TYPE_TRAP);
    set_idt_descriptor(2,(uint64)nmi,IST_1,TYPE_INTRPT);
    set_idt_descriptor(3,(uint64)int3,IST_1,TYPE_TRAP);
    set_idt_descriptor(4,(uint64)overflow,IST_1,TYPE_TRAP);
    set_idt_descriptor(5,(uint64)bounds,IST_1,TYPE_TRAP);
    set_idt_descriptor(6,(uint64)undefined_opcode,IST_1,TYPE_TRAP);
    set_idt_descriptor(7,(uint64)dev_not_available,IST_1,TYPE_TRAP);
    set_idt_descriptor(8,(uint64)double_fault,IST_1,TYPE_TRAP);
    set_idt_descriptor(9,(uint64)coprocessor_segment_overrun,IST_1,TYPE_TRAP);
    set_idt_descriptor(10,(uint64)invalid_TSS,IST_1,TYPE_TRAP);
    set_idt_descriptor(11,(uint64)segment_not_present,IST_1,TYPE_TRAP);
    set_idt_descriptor(12,(uint64)stack_segment_fault,IST_1,TYPE_TRAP);
    set_idt_descriptor(13,(uint64)general_protection,IST_1,TYPE_TRAP);
    set_idt_descriptor(14,(uint64)page_fault,IST_1,TYPE_TRAP);
    //15 Intel reserved. Do not use.
    set_idt_descriptor(16,(uint64)x87_FPU_error,IST_1,TYPE_TRAP);
    set_idt_descriptor(17,(uint64)alignment_check,IST_1,TYPE_TRAP);
    set_idt_descriptor(18,(uint64)machine_check,IST_1,TYPE_TRAP);
    set_idt_descriptor(19,(uint64)SIMD_exception,IST_1,TYPE_TRAP);
    set_idt_descriptor(20,(uint64)virtualization_exception,IST_1,TYPE_TRAP);
    //中断
    set_idt_descriptor(0x20,(uint64)apic_timer,IST_1,TYPE_INTRPT);
    set_idt_descriptor(0x31,(uint64)keyboard,IST_1,TYPE_INTRPT);
    set_idt_descriptor(0x32,(uint64)hpet,IST_1,TYPE_INTRPT);

    lidt(&idt_ptr);
}