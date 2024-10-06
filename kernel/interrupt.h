#ifndef _INTERRUPT_H
#define _INTERRUPT_H

#include "linkage.h"
#include "printk.h"
#include "moslib.h"
#include "apic.h"
#include "hpet.h"



// 定义中断处理函数
void ignore(void);
void keyboard(void);
void apic_timer(void);
void hpet(void);


// 定义0-20异常处理函数
void divide_error();
void debug();
void nmi();
void int3();
void overflow();
void bounds();
void undefined_opcode();
void dev_not_available();
void double_fault();
void coprocessor_segment_overrun();
void invalid_TSS();
void segment_not_present();
void stack_segment_fault();
void general_protection();
void page_fault();
void x87_FPU_error();
void alignment_check();
void machine_check();
void SIMD_exception();
void virtualization_exception();

#endif