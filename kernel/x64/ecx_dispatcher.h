#pragma once
#include "cpu_registers.h"
#include "moslib.h"

//CPU异常函数签名
typedef void (*exception_handler_f)(cpu_registers_t *regs);

int32 register_exception(int32 vector, exception_handler_f handler);
int32 unregister_exception(int32 vector);

// 定义0-20异常处理函数
void do_divide_error(cpu_registers_t *regs);
void do_debug(cpu_registers_t *regs);
void do_nmi(cpu_registers_t *regs);
void do_int3(cpu_registers_t *regs);
void do_overflow(cpu_registers_t *regs) ;
void do_bounds(cpu_registers_t *regs);
void do_undefined_opcode(cpu_registers_t *regs);
void do_dev_not_available(cpu_registers_t *regs);
void do_double_fault(cpu_registers_t *regs);
void do_coprocessor_segment_overrun(cpu_registers_t *regs);
void do_invalid_tss(cpu_registers_t *regs);
void do_segment_not_present(cpu_registers_t *regs);
void do_stack_segment_fault(cpu_registers_t *regs);
void do_general_protection(cpu_registers_t *regs);
void do_page_fault(cpu_registers_t *regs);
void do_x87_FPU_error(cpu_registers_t *regs) ;
void do_alignment_check(cpu_registers_t *regs) ;
void do_machine_check(cpu_registers_t *regs);
void do_SIMD_exception(cpu_registers_t *regs);
void do_virtualization_exception(cpu_registers_t *regs) ;