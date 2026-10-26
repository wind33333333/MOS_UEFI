#include "cpu_registers.h"

.code64
.section .text, "ax"
.altmacro

# ---------------------------------------------------------
# 栅格生成宏 (每个宏展开后严格占据 16 字节物理空间)
# ---------------------------------------------------------

/* 异常桩 - 无错误码 (自动补 0，跳向异常处理中枢) */
.macro EXC_NOERR vector
    .p2align 4                  /* 🌟 强制对齐到 16 字节边界，不足自动补 NOP */
    pushq $0                    /* 填补 dummy Error Code (2 字节) */
    pushq $\vector              /* 压入向量号 (2~5 字节) */
    jmp exception_com_stub      /* 跳转到异常公共入口 (5 字节) */
.endm

/* 异常桩 - 有硬件错误码 (跳向异常处理中枢) */
.macro EXC_ERR vector
    .p2align 4
    pushq $\vector
    jmp exception_com_stub
.endm


/* 硬件 IRQ 桩 (跳向外设中断处理中枢) */
.macro IRQ_STUB vector
    .p2align 4
    pushq $0                    /* IRQ 无错误码，补 0 */
    pushq $\vector
    jmp irq_com_stub            /* 👉 跳转到 IRQ 专属公共入口 */
.endm

# ========================================================
# 2. 定长栅格跳板阵列 (Fixed-Stride Array)
# ========================================================
.global interrupt_entry_base
.p2align 4                      /* 确保整个基址起始于 16 字节对齐处 */
interrupt_entry_base:

/* --- 前 32 个：CPU 异常与陷阱 --- */
EXC_NOERR 0     /* 0: #DE 除零异常 */
EXC_NOERR 1     /* 1: #DB 调试异常 */

/* 2: NMI 专属通道。严格遵守 16 字节栅格，但跳向 Paranoid 入口 */
.p2align 4
pushq $0        /* NMI 无错误码，补 0 */
pushq $2        /* 压入向量号 2 */
jmp nmi_entry   /* 👉 跳向 NMI 专属中枢 */

EXC_NOERR 3     /* 3: #BP 断点 */
EXC_NOERR 4     /* 4: #OF 溢出 */
EXC_NOERR 5     /* 5: #BR 越界 */
EXC_NOERR 6     /* 6: #UD 无效指令 */
EXC_NOERR 7     /* 7: #NM 设备不可用 */
EXC_ERR   8     /* 8: #DF 双重错误 (有 Error Code) */
EXC_NOERR 9     /* 9: 协处理器段越界 */
EXC_ERR   10    /* 10: #TS 无效 TSS */
EXC_ERR   11    /* 11: #NP 段不存在 */
EXC_ERR   12    /* 12: #SS 栈段错误 */
EXC_ERR   13    /* 13: #GP 常规保护错误 */
EXC_ERR   14    /* 14: #PF 缺页异常 */
EXC_NOERR 15    /* 15: 保留 */
EXC_NOERR 16    /* 16: #MF 浮点异常 */
EXC_ERR   17    /* 17: #AC 对齐检查 */
EXC_NOERR 18    /* 18: #MC 机器检查 */
EXC_NOERR 19    /* 19: #XM SIMD 异常 */
EXC_NOERR 20    /* 20: #VE 虚拟化异常 */
EXC_ERR   21    /* 21: #CP 控制保护 */

/* 22 ~ 28 保留异常 (循环生成) */
.set k, 22
.rept 7
    EXC_NOERR %k
    .set k, k + 1
.endr

EXC_ERR   29    /* 29: #VC VMM 通信异常 */
EXC_ERR   30    /* 30: #SX 安全异常 */
EXC_NOERR 31    /* 31: 保留 */

/* --- 32 ~ 255：外设硬件 IRQ 区 (循环生成) --- */
.set i, 32
.rept 224
    IRQ_STUB %i
    .set i, i + 1
.endr

# ========================================================
# 3. 三大核心处理中枢 (隔离执行流)
# ========================================================

# ---------------------------------------------------------
# A. 异常中枢 (Exception): 不换栈，允许挂起/休眠
# ---------------------------------------------------------
.extern c_exception_dispatcher

exception_com_stub:
    testb $3, 24(%rsp)          # 检查被中断时的特权级
    jz 1f
    swapgs                      # 如果来自 Ring 3，切入内核 GS
1:
    SAVE_ALL_REGS

    movq %rsp, %rdi             # 参数 1: pt_regs* 传入
    call c_exception_dispatcher # ⚠️ 直接在原线程栈运行，允许引发 IO 和休眠！

    RESTORE_ALL_REGS
    addq $16, %rsp              # 清理 vector 和 dummy error code

    testb $3, 8(%rsp)
    jz 2f
    swapgs                      # 返回 Ring 3 前恢复 GS
2:
    iretq

# ---------------------------------------------------------
# B. 硬件中断中枢 (Hardware IRQ): 无脑切换 HardIRQ 栈，绝对禁止休眠
# ---------------------------------------------------------
.extern c_irq_dispatcher
.extern check_and_schedule
.set HARDIRQ_RSP_TOP, 0         # 假设 Per-CPU 结构中 HardIRQ 栈顶位于 offset 0

irq_com_stub:
    testb $3, 24(%rsp)
    jz 1f
    swapgs
1:
    SAVE_ALL_REGS

    movq %rsp, %rdi             # 参数 1: 旧栈的 pt_regs*
    movq %rsp, %rbx             # 借用 rbx 保存原线程栈指针

    /* 👉 无脑切换 HardIRQ 栈 (极速，无分支) */
    movq %gs:HARDIRQ_RSP_TOP, %rsp
    andq $-16, %rsp             # 强制 16 字节对齐

    call c_irq_dispatcher       # ⚠️ 专属栈运行，绝对禁止主动休眠！

    movq %rbx, %rsp             # 恢复原线程栈

    RESTORE_ALL_REGS
    addq $16, %rsp

    call check_and_schedule

    /* --- 抢占式调度拦截点 --- */
    testb $3, 8(%rsp)           # 检查即将返回的特权级
    jz 2f                       # 早期微内核: 返回 Ring 0 不抢占

    /* 检查是否需要调度 (假设 need_resched 标志在当前 CPU 某个偏移，如 offset 8) */
    # cmpl $1, %gs:8
    # jne 3f
    # call schedule             # 如果需要，执行上下文切换

3:
    swapgs
2:
    iretq

# ---------------------------------------------------------
# C. NMI 专属中枢 (Paranoid): 基于 rdgsbase 的精准探测，防嵌套死锁
# ---------------------------------------------------------
.extern do_nmi

nmi_entry:
    /* 注意: NMI 的 dummy error code 和 vector 2 已经在跳板里压入了 */
    SAVE_ALL_REGS

    /* 🌟 Paranoid 防御: 64 位精准检测 GS 归属，杜绝 `swapgs` 缝隙危机 */
    rdgsbase %rbx
    testq %rbx, %rbx            # 判断符号位: 负数(高半区) 为内核态
    js 1f
    swapgs                      # 符号位 0(低半区)，说明在用户态被打断，切换 GS
1:
    movq %rsp, %rdi
    call do_nmi

    testq %rbx, %rbx            # 取出保存的 GS_BASE 再次验证
    js 2f
    swapgs                      # 恢复用户态 GS
2:
    RESTORE_ALL_REGS
    addq $16, %rsp
    iretq