#include "idt.h"
#include "gdt.h"
#include "interrupt.h"

__attribute__((section(".init_text"))) void init_idt(UINT8 bsp_flags) {
    if (bsp_flags) {
        //初始化中断向量表为默认中断
        for (int i = 0; i < 256; i++) {
            SET_GATE(idt_ptr.base, i, ignore, IST_1, TYPE_INT);
        }

        //初始化0-20异常
        SET_GATE(idt_ptr.base,0,divide_error,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,1,debug,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,2,nmi,IST_1,TYPE_INT);
        SET_GATE(idt_ptr.base,3,int3,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,4,overflow,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,5,bounds,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,6,undefined_opcode,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,7,dev_not_available,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,8,double_fault,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,9,coprocessor_segment_overrun,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,10,invalid_TSS,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,11,segment_not_present,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,12,stack_segment_fault,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,13,general_protection,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,14,page_fault,IST_1,TYPE_TRAP);
        //15 Intel reserved. Do not use.
        SET_GATE(idt_ptr.base,16,x87_FPU_error,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,17,alignment_check,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,18,machine_check,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,19,SIMD_exception,IST_1,TYPE_TRAP);
        SET_GATE(idt_ptr.base,20,virtualization_exception,IST_1,TYPE_TRAP);
        //中断
        SET_GATE(idt_ptr.base,0x20,apic_timer,IST_1,TYPE_INT);
        SET_GATE(idt_ptr.base,0x31,keyboard,IST_1,TYPE_INT);
        SET_GATE(idt_ptr.base,0x32,hpet,IST_1,TYPE_INT);
    }

    __asm__ __volatile__(
            "lidt (%0)  \n\t"
            ::"r"(&idt_ptr):);
    return;
}