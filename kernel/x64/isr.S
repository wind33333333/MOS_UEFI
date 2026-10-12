.code64
.section .text, "ax"

# ========================================================
# 1. 通用寄存器现场宏 (彻底消灭重复的 push/pop)
# ========================================================
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

# ========================================================
# 2. 存根宏定义
# ========================================================
.macro ISR_NOERRCODE vector
.global isr\vector
isr\vector:
    pushq $0                 # 补齐 dummy 错误码
    pushq $\vector          # 压入中断号
    jmp isr_common_stub
.endm

.macro ISR_ERRCODE vector
.global isr\vector
isr\vector:
    pushq $\vector          # CPU 已压入错误码，直接压中断号
    jmp isr_common_stub
.endm

# ========================================================
# 3. 0~31 号 CPU 内部异常 (清晰标注，#DF 完美并入通用体系)
# ========================================================
ISR_NOERRCODE 0    # #DE 除零错误
ISR_NOERRCODE 1    # #DB 调试异常
# 向量 2 (NMI) 保持下方独立实现，此处不生成
ISR_NOERRCODE 3    # #BP 断点
ISR_NOERRCODE 4    # #OF 溢出
ISR_NOERRCODE 5    # #BR 越界
ISR_NOERRCODE 6    # #UD 无效指令
ISR_NOERRCODE 7    # #NM 设备不可用
ISR_ERRCODE   8    # #DF 双重错误 (通过 IST1 切栈，进通用存根后在 C 语言原地终止)
ISR_NOERRCODE 9    # 协处理器段溢出
ISR_ERRCODE   10   # #TS 无效TSS
ISR_ERRCODE   11   # #NP 段不存在
ISR_ERRCODE   12   # #SS 栈段错误
ISR_ERRCODE   13   # #GP 常规保护错误
ISR_ERRCODE   14   # #PF 页错误
ISR_NOERRCODE 15   # Intel 保留
ISR_NOERRCODE 16   # #MF 浮点错误
ISR_ERRCODE   17   # #AC 对齐检查
ISR_NOERRCODE 18   # #MC 机器检查
ISR_NOERRCODE 19   # #XM SIMD 异常
ISR_NOERRCODE 20   # #VE 虚拟化异常
ISR_ERRCODE   21   # #CP 控制流保护异常
# 22 ~ 31 保留异常
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

# ========================================================
# 4. 32~255 号外部硬件中断 (用 .rept 消除 224 行纯手敲代码)
# ========================================================
.altmacro
.macro BATCH_GEN_EXT_ISR from, to
    .set i, \from
    .rept \to - \from + 1
        ISR_NOERRCODE %i
        .set i, i + 1
    .endr
.endm

BATCH_GEN_EXT_ISR 32, 255

# ========================================================
# 5. 通用中断跳板 (服务除 NMI 外的所有中断与异常)
# ========================================================
.extern c_interrupt_dispatcher

isr_common_stub:
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
# 6. NMI 专属极简存根 (保留 Paranoid 保证安全，杜绝代码膨胀)
# ========================================================
.global isr2
.extern do_nmi
.set MSR_GS_BASE, 0xC0000101

isr2:
    pushq $0                # 补齐 dummy 错误码
    pushq $2                # 压入中断号 2
    SAVE_ALL_REGS           # 🌟 复用宏

    # Paranoid 偏执状态检测
    xorl %ebx, %ebx         # EBX: 0 = 未执行 swapgs, 1 = 执行了 swapgs
    movl $MSR_GS_BASE, %ecx
    rdmsr
    testl %edx, %edx        # 检查是否为高半区内核地址
    js 1f
    swapgs
    movl $1, %ebx
1:
    movq %rsp, %rdi
    call do_nmi             # 调用独立的 NMI 处理函数 (内部严禁调用调度器)

    testl %ebx, %ebx
    jz 2f
    swapgs
2:
    RESTORE_ALL_REGS        # 🌟 复用宏
    addq $16, %rsp
    iretq

# ========================================================
# 7. 中断处理函数指针表 (用 .rept 压缩至 10 行以内)
# ========================================================
.section .init_data, "wa"
.global isr_stub_table

.macro EMIT_TABLE_ENTRY vec
    .quad isr\vec
.endm

isr_stub_table:
    .set j, 0
    .rept 256
        EMIT_TABLE_ENTRY %j
        .set j, j + 1
    .endr