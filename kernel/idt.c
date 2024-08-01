#include "idt.h"

__attribute__((section(".init_text"))) void idt_init(unsigned char bsp_flags) {
    if (bsp_flags) {
        //初始化中断向量表为默认中断
        for (int i = 0; i < 256; i++) {
            SET_GATE(IDT_Table, i, ignore, IST_1, TYPE_INT);
        }

        //初始化0-20异常
        SET_GATE(IDT_Table,0,divide_error,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,1,debug,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,2,nmi,IST_1,TYPE_INT);
        SET_GATE(IDT_Table,3,int3,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,4,overflow,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,5,bounds,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,6,undefined_opcode,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,7,dev_not_available,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,8,double_fault,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,9,coprocessor_segment_overrun,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,10,invalid_TSS,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,11,segment_not_present,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,12,stack_segment_fault,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,13,general_protection,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,14,page_fault,IST_1,TYPE_TRAP);
        //15 Intel reserved. Do not use.
        SET_GATE(IDT_Table,16,x87_FPU_error,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,17,alignment_check,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,18,machine_check,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,19,SIMD_exception,IST_1,TYPE_TRAP);
        SET_GATE(IDT_Table,20,virtualization_exception,IST_1,TYPE_TRAP);

        //中断
        SET_GATE(IDT_Table,0x20,apic_timer,IST_1,TYPE_INT);
        SET_GATE(IDT_Table,0x31,keyboard,IST_1,TYPE_INT);
        SET_GATE(IDT_Table,0x32,hpet,IST_1,TYPE_INT);


    }

    __asm__ __volatile__(
            "lidt (%0)  \n\t"
            ::"r"(&IDT_POINTER):);
    return;
}