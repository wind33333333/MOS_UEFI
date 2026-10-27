#pragma once
#include "cpu_registers.h"
#include "moslib.h"

// ==========================================
// Intel x86_64 架构保留的 0-31 号异常向量宏定义
// 参照 Intel SDM Volume 3: Chapter 6
// ==========================================
#define X86_EXC_DE   0   // Divide Error (#DE, 除零异常)
#define X86_EXC_DB   1   // Debug (#DB, 调试异常)
#define X86_EXC_NMI  2   // Non-Maskable Interrupt (不可屏蔽中断)
#define X86_EXC_BP   3   // Breakpoint (#BP, 断点异常, INT3)
#define X86_EXC_OF   4   // Overflow (#OF, 溢出异常)
#define X86_EXC_BR   5   // BOUND Range Exceeded (#BR, 越界异常)
#define X86_EXC_UD   6   // Invalid Opcode (#UD, 无效操作码/未定义指令)
#define X86_EXC_NM   7   // Device Not Available (#NM, 设备不可用/FPU缺失)
#define X86_EXC_DF   8   // Double Fault (#DF, 双重错误)
#define X86_EXC_CSO  9   // Coprocessor Segment Overrun (协处理器段越界, 现已废弃)
#define X86_EXC_TS   10  // Invalid TSS (#TS, 无效的任务状态段)
#define X86_EXC_NP   11  // Segment Not Present (#NP, 段不存在)
#define X86_EXC_SS   12  // Stack Segment Fault (#SS, 栈段错误)
#define X86_EXC_GP   13  // General Protection (#GP, 一般保护性错误)
#define X86_EXC_PF   14  // Page Fault (#PF, 缺页错误)
// 15号由 Intel 保留
#define X86_EXC_MF   16  // x87 FPU Floating-Point Error (#MF, 浮点数学错误)
#define X86_EXC_AC   17  // Alignment Check (#AC, 对齐检查错误)
#define X86_EXC_MC   18  // Machine Check (#MC, 机器检查错误, 硬件故障)
#define X86_EXC_XM   19  // SIMD Floating-Point Exception (#XM/#XF, SIMD 浮点异常)
#define X86_EXC_VE   20  // Virtualization Exception (#VE, 虚拟化异常)
#define X86_EXC_CP   21  // Control Protection Exception (#CP, 控制流保护异常)
// 22-31号由 Intel 保留，供未来硬件特性使用

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