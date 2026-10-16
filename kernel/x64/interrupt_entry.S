#include "cpu_registers.h"

.code64
.section .text, "ax"


# ========================================================
# 1. 无错误码中断入口模板
# ========================================================
.macro INTERRUPT_NOERRCODE_ENTRY vector
.global interrupt\vector
interrupt\vector:
    .p2align 4
    pushq $0                # 补齐 dummy 错误码
    pushq $\vector          # 压入中断号
    jmp interrupt_com_stub
.endm


# ========================================================
# 2. 有错误码中断入口模板
# ========================================================
.macro INTERRUPT_ERRCODE_ENTRY vector
.global interrupt\vector
interrupt\vector:
    .p2align 4
    pushq $\vector          # CPU 已压入错误码，直接压中断号
    jmp interrupt_com_stub
.endm


# ========================================================
# 256个中断入口
# ========================================================
.p2align 4
.global interrupt_entry_base
interrupt_entry_base:
# ========================================================
# 3. 0~31 号 CPU 内部异常 (清晰标注，#DF 完美并入通用体系)
# ========================================================
INTERRUPT_NOERRCODE_ENTRY 0    # #DE 除零错误
INTERRUPT_NOERRCODE_ENTRY 1    # #DB 调试异常
# 🌟 向量 2 (NMI) 占满自己的 16 字节格子，直接跳往后方的独立 Paranoid 流程
.p2align 4
jmp interrupt2
INTERRUPT_NOERRCODE_ENTRY 3    # #BP 断点
INTERRUPT_NOERRCODE_ENTRY 4    # #OF 溢出
INTERRUPT_NOERRCODE_ENTRY 5    # #BR 越界
INTERRUPT_NOERRCODE_ENTRY 6    # #UD 无效指令
INTERRUPT_NOERRCODE_ENTRY 7    # #NM 设备不可用
INTERRUPT_ERRCODE_ENTRY   8    # #DF 双重错误 (通过 IST1 切栈，进通用存根后在 C 语言原地终止)
INTERRUPT_NOERRCODE_ENTRY 9    # 协处理器段溢出
INTERRUPT_ERRCODE_ENTRY   10   # #TS 无效TSS
INTERRUPT_ERRCODE_ENTRY   11   # #NP 段不存在
INTERRUPT_ERRCODE_ENTRY   12   # #SS 栈段错误
INTERRUPT_ERRCODE_ENTRY   13   # #GP 常规保护错误
INTERRUPT_ERRCODE_ENTRY   14   # #PF 页错误
INTERRUPT_NOERRCODE_ENTRY 15   # Intel 保留
INTERRUPT_NOERRCODE_ENTRY 16   # #MF 浮点错误
INTERRUPT_ERRCODE_ENTRY   17   # #AC 对齐检查
INTERRUPT_NOERRCODE_ENTRY 18   # #MC 机器检查
INTERRUPT_NOERRCODE_ENTRY 19   # #XM SIMD 异常
INTERRUPT_NOERRCODE_ENTRY 20   # #VE 虚拟化异常
INTERRUPT_ERRCODE_ENTRY   21   # #CP 控制流保护异常
# 22 ~ 31 保留异常
.altmacro
.set k, 22
.rept 10
    INTERRUPT_NOERRCODE_ENTRY %k
    .set k, k + 1
.endr

# --- 32~255 外设硬件中断区 ---
.set i, 32
.rept 256 - 32
    INTERRUPT_NOERRCODE_ENTRY %i
    .set i, i + 1
.endr


.extern c_interrupt_dispatcher
# ========================================================
# 通用中断跳板 (服务除 NMI 外的所有中断与异常)
# ========================================================
interrupt_com_stub:
    # 检查进入前的特权级 (CS 位于 rsp + 24)
    testb $3, 24(%rsp)
    jz 1f
    swapgs                  # 来自 Ring 3，切入内核 GS
1:
    SAVE_ALL_REGS           # 保存 15 个通用寄存器

    movq %rsp, %rdi         # pt_regs 指针通过 RDI 传给 C 语言
    call c_interrupt_dispatcher

    # 退出路径恢复
    RESTORE_ALL_REGS        # 恢复 15 个通用寄存器
    addq $16, %rsp          # 跨过 int_no 与 err_code

    testb $3, 8(%rsp)       # 检查返回目标 CS (位于 rsp + 8)
    jz 2f
    swapgs                  # 返回 Ring 3 前切回用户 GS
2:
    iretq

# ========================================================
# NMI 专属极简存根 (保留 Paranoid 保证安全，杜绝代码膨胀)
# ========================================================
.extern do_nmi
interrupt2:
    pushq $0                # 补齐 dummy 错误码
    pushq $2                # 压入中断号 2
    SAVE_ALL_REGS           # 🌟 复用宏

    # Paranoid 偏执状态检测
    rdgsbase %rbx
    testl %ebx, %ebx        # 检查是否为高半区内核地址
    js 1f
    swapgs
1:
    movq %rsp, %rdi
    call do_nmi             # 调用独立的 NMI 处理函数 (内部严禁调用调度器)

    testl %ebx, %ebx
    js 2f
    swapgs
2:
    RESTORE_ALL_REGS        # 🌟 复用宏
    addq $16, %rsp
    iretq





