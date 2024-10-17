#include "printk.h"
#include "moslib.h"
#include "apic.h"


void do_apic_timer(void) {
    color_printk(RED,BLACK,"apic timer interrupt\n");

//    UINT64 time = 0xF000000;
//    APIC_SET_TSCDEADLINE(time);
    EOI();
    return;
}

void do_hpet(void) {

    color_printk(RED,BLACK,"hpet interrupt\n");
    EOI();
    return;
}

void do_apic_spurious(void) {
    color_printk(RED,BLACK,"apic spurious interrupt\n");
    EOI();
    return;
}

void do_keyboard(void) {

    __asm__ __volatile__("in    $0x60,%%al" :::);
    color_printk(RED,BLACK,"keyboard interrupt\n");

    EOI();
    return;
}

void do_ignore(void) {

    color_printk(RED,BLACK,"Unknown interrupt or fault at RIP\n");
    while (1);
    return;
}


void do_divide_error(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_divide_error(0),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);
    while (1);
}


void do_debug(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_debug(1),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n", error_code,
                 rsp, *p);
    while (1);
}


void do_nmi(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_nmi(2),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n", error_code,
                 rsp, *p);
    while (1);
}


void do_int3(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_int3(3),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n", error_code,
                 rsp, *p);
    while (1);
}


void do_overflow(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_overflow(4),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);
    while (1);
}


void do_bounds(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_bounds(5),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);
    while (1);
}


void do_undefined_opcode(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_undefined_opcode(6),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);
    while (1);
}


void do_dev_not_available(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_dev_not_available(7),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);
    while (1);
}


void do_double_fault(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_double_fault(8),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);
    while (1);
}


void do_coprocessor_segment_overrun(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK,
                 "do_coprocessor_segment_overrun(9),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);
    while (1);
}


void do_invalid_TSS(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_invalid_TSS(10),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);

    if (error_code & 0x01)
        color_printk(RED, BLACK,
                     "The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (error_code & 0x02)
        color_printk(RED, BLACK, "Refers to a gate descriptor in the IDT;\n");
    else
        color_printk(RED, BLACK, "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((error_code & 0x02) == 0)
        if (error_code & 0x04)
            color_printk(RED, BLACK, "Refers to a segment or gate descriptor in the LDT;\n");
        else
            color_printk(RED, BLACK, "Refers to a descriptor in the current GDT;\n");

    color_printk(RED, BLACK, "Segment Selector Index:%#010x\n", error_code & 0xfff8);

    while (1);
}


void do_segment_not_present(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK,
                 "do_segment_not_present(11),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);

    if (error_code & 0x01)
        color_printk(RED, BLACK,
                     "The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (error_code & 0x02)
        color_printk(RED, BLACK, "Refers to a gate descriptor in the IDT;\n");
    else
        color_printk(RED, BLACK, "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((error_code & 0x02) == 0)
        if (error_code & 0x04)
            color_printk(RED, BLACK, "Refers to a segment or gate descriptor in the LDT;\n");
        else
            color_printk(RED, BLACK, "Refers to a descriptor in the current GDT;\n");

    color_printk(RED, BLACK, "Segment Selector Index:%#010x\n", error_code & 0xfff8);

    while (1);
}


void do_stack_segment_fault(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK,
                 "do_stack_segment_fault(12),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);

    if (error_code & 0x01)
        color_printk(RED, BLACK,
                     "The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (error_code & 0x02)
        color_printk(RED, BLACK, "Refers to a gate descriptor in the IDT;\n");
    else
        color_printk(RED, BLACK, "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((error_code & 0x02) == 0)
        if (error_code & 0x04)
            color_printk(RED, BLACK, "Refers to a segment or gate descriptor in the LDT;\n");
        else
            color_printk(RED, BLACK, "Refers to a descriptor in the current GDT;\n");

    color_printk(RED, BLACK, "Segment Selector Index:%#010x\n", error_code & 0xfff8);

    while (1);
}


void do_general_protection(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK,
                 "do_general_protection(13),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);

    if (error_code & 0x01)
        color_printk(RED, BLACK,
                     "The exception occurred during delivery of an event external to the program,such as an interrupt or an earlier exception.\n");

    if (error_code & 0x02)
        color_printk(RED, BLACK, "Refers to a gate descriptor in the IDT;\n");
    else
        color_printk(RED, BLACK, "Refers to a descriptor in the GDT or the current LDT;\n");

    if ((error_code & 0x02) == 0)
        if (error_code & 0x04)
            color_printk(RED, BLACK, "Refers to a segment or gate descriptor in the LDT;\n");
        else
            color_printk(RED, BLACK, "Refers to a descriptor in the current GDT;\n");

    color_printk(RED, BLACK, "Segment Selector Index:%#010x\n", error_code & 0xfff8);

    while (1);
}


void do_page_fault(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    UINT64 cr2 = 0;

    __asm__ __volatile__("movq	%%cr2,	%0":"=r"(cr2)::"memory");

    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_page_fault(14),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);

    if (!(error_code & 0x01))
        color_printk(RED, BLACK, "Page Not-Present,\t");

    if (error_code & 0x02)
        color_printk(RED, BLACK, "Write Cause Fault,\t");
    else
        color_printk(RED, BLACK, "Read Cause Fault,\t");

    if (error_code & 0x04)
        color_printk(RED, BLACK, "Fault in user(3)\t");
    else
        color_printk(RED, BLACK, "Fault in supervisor(0,1,2)\t");

    if (error_code & 0x08)
        color_printk(RED, BLACK, ",Reserved Bit Cause Fault\t");

    if (error_code & 0x10)
        color_printk(RED, BLACK, ",Instruction fetch Cause Fault");

    color_printk(RED, BLACK, "\n");

    color_printk(RED, BLACK, "CR2:%#018lx\n", cr2);

    while (1);
}


void do_x87_FPU_error(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_x87_FPU_error(16),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);
    while (1);
}


void do_alignment_check(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_alignment_check(17),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);
    while (1);
}


void do_machine_check(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_machine_check(18),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);
    while (1);
}


void do_SIMD_exception(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK, "do_SIMD_exception(19),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);
    while (1);
}


void do_virtualization_exception(UINT64 rsp, UINT64 error_code) {
    UINT64 *p = NULL;
    p = (UINT64 *) (rsp + 0x98);
    color_printk(RED, BLACK,
                 "do_virtualization_exception(20),ERROR_CODE:%#018lx,RSP:%#018lx,RIP:%#018lx\n",
                 error_code, rsp, *p);
    while (1);
}

