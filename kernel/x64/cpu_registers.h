#pragma once

/* ====================================================================
 * 1. 汇编专区 (仅在 .S 汇编预处理时生效，C 编译器自动忽略)
 * ==================================================================== */
#ifdef __ASSEMBLER__

/* 1. 通用寄存器现场宏 (避免在行首使用 #，改用 C 注释防止预处理器报错) */
.macro SAVE_ALL_REGS
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
.endm

.macro RESTORE_ALL_REGS
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
.endm

#else

/* ====================================================================
 * 2. C 语言专区 (仅在 .c 编译时生效，汇编器自动忽略)
 * ==================================================================== */
#include "moslib.h"

/**
 * @brief x86_64 统一中断/系统调用现场结构体 (共 22 个寄存器/元数据，176 字节)
 *
 * 内存分布顺序：
 * [低地址 %rsp] -> r15 ... rax -> int_no -> err_code -> rip ... ss -> [高地址]
 */
typedef struct {
    /* 1. 软件压入的通用寄存器 (由 SAVE_ALL_REGS 倒序压入) */
    uint64 r15;       /* 偏移 0x00 (0)   */
    uint64 r14;       /* 偏移 0x08 (8)   */
    uint64 r13;       /* 偏移 0x10 (16)  */
    uint64 r12;       /* 偏移 0x18 (24)  */
    uint64 r11;       /* 偏移 0x20 (32)  - 用户态原 R11 / RFLAGS */
    uint64 r10;       /* 偏移 0x28 (40)  - 系统调用第 4 个参数 */
    uint64 r9;        /* 偏移 0x30 (48)  - 系统调用第 6 个参数 */
    uint64 r8;        /* 偏移 0x38 (56)  - 系统调用第 5 个参数 */
    uint64 rbp;       /* 偏移 0x40 (64)  */
    uint64 rdi;       /* 偏移 0x48 (72)  - 系统调用第 1 个参数 */
    uint64 rsi;       /* 偏移 0x50 (80)  - 系统调用第 2 个参数 */
    uint64 rdx;       /* 偏移 0x58 (88)  - 系统调用第 3 个参数 */
    uint64 rcx;       /* 偏移 0x60 (96)  - 用户态原 RCX / RIP */
    uint64 rbx;       /* 偏移 0x68 (104) */
    uint64 rax;       /* 偏移 0x70 (112) - 系统调用号 / 系统调用返回值 */

    /* 2. 存根手动伪造的元数据 */
    uint64 no;        /* 偏移 0x78 (120) - 中断号或系统调用号 */
    uint64 err_code;  /* 偏移 0x80 (128) - 错误码 (syscall 填 0) */

    /* 3. 硬件/跳板压入的核心执行现场 */
    uint64 rip;       /* 偏移 0x88 (136) - 用户态返回地址 (来自 syscall 硬件暂存的 RCX) */
    uint64 cs;        /* 偏移 0x90 (144) - 用户代码段选择子 (例如 0x1B 或 0x23) */
    uint64 rflags;    /* 偏移 0x98 (152) - 用户态标志寄存器 (来自 syscall 硬件暂存的 R11) */
    uint64 rsp;       /* 偏移 0xA0 (160) - 用户态栈指针 */
    uint64 ss;        /* 偏移 0xA8 (168) - 用户数据段选择子 (例如 0x23 或 0x1B) */
} __attribute__((packed)) cpu_registers_t;

#endif /* __ASSEMBLER__ */