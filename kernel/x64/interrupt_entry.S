#include "cpu_registers.h"

.code64
.section .text, "ax"

# ========================================================
# 1. 通用中断桩生成宏 (强制 16 字节边界先行对齐)
# ========================================================
.macro ISR_STUB_NOERR vec
    .p2align 4
    pushq $0                    # 补齐 dummy 错误码 (2 字节)
    pushq $\vec                 # 压入中断号 (2~5 字节)
    jmp interrupt_com_stub      # 跳转通用跳板 (5 字节)
.endm

.macro ISR_STUB_ERR vec
    .p2align 4
    pushq $\vec                 # 硬件已压入错误码，直接压中断号 (2~5 字节)
    jmp interrupt_com_stub      # 跳转通用跳板 (5 字节)
.endm

# ========================================================
# 2. 256 个中断入口连续物理阵列 (纯基址导出)
# ========================================================
.p2align 4
.global interrupt_entry_base
interrupt_entry_base:

# --- 0~31 CPU 异常区 ---
ISR_STUB_NOERR 0    # #DE
ISR_STUB_NOERR 1    # #DB

# 向量 2 (NMI) 占满自己的 16 字节栅格，直接跳转独立 Paranoid 处理流程
.p2align 4
jmp interrupt2_nmi_entry

ISR_STUB_NOERR 3    # #BP
ISR_STUB_NOERR 4    # #OF
ISR_STUB_NOERR 5    # #BR
ISR_STUB_NOERR 6    # #UD
ISR_STUB_NOERR 7    # #NM
ISR_STUB_ERR   8    # #DF (双重错误)
ISR_STUB_NOERR 9    # 协处理器段溢出
ISR_STUB_ERR   10   # #TS
ISR_STUB_ERR   11   # #NP
ISR_STUB_ERR   12   # #SS
ISR_STUB_ERR   13   # #GP
ISR_STUB_ERR   14   # #PF (缺页异常)
ISR_STUB_NOERR 15   # 保留
ISR_STUB_NOERR 16   # #MF
ISR_STUB_ERR   17   # #AC
ISR_STUB_NOERR 18   # #MC
ISR_STUB_NOERR 19   # #XM
ISR_STUB_NOERR 20   # #VE
ISR_STUB_ERR   21   # #CP (控制保护异常)

# 22 ~ 31 保留异常
.altmacro
.set k, 22
.rept 10
    ISR_STUB_NOERR %k
    .set k, k + 1
.endr

# --- 32~255 外设硬件中断区 (全部无错误码) ---
.set i, 32
.rept 256 - 32
    ISR_STUB_NOERR %i
    .set i, i + 1
.endr

# ========================================================
# 3. 通用中断跳板 (服务除 NMI 外的所有中断与异常)
# ========================================================
.extern c_interrupt_dispatcher

interrupt_com_stub:
    # 检查进入前的特权级 (CS 位于 rsp + 24)
    testb $3, 24(%rsp)
    jz 1f
    swapgs                      # 来自 Ring 3，切入内核 GS
1:
    SAVE_ALL_REGS               # 保存 15 个通用寄存器

    cld                         # 🌟 防御：确保 DF=0，杜绝用户态 std 导致 C 库内存踩踏
    movq %rsp, %rdi             # cpu_registers_t 结构体指针送入 RDI
    call c_interrupt_dispatcher

    RESTORE_ALL_REGS            # 恢复 15 个通用寄存器
    addq $16, %rsp              # 跨过 int_no 与 err_code (16 字节)

    # 检查返回目标特权级 (CS 位于 rsp + 8)
    testb $3, 8(%rsp)
    jz 2f
    swapgs                      # 即将还阳回 Ring 3，切回用户 GS
2:
    iretq                       # 硬件原子恢复 RIP, CS, RFLAGS, RSP, SS

# ========================================================
# 4. NMI 专属存根 (Paranoid 64 位精准状态探测)
# ========================================================
.extern do_nmi

interrupt2_nmi_entry:
    pushq $0                    # 补齐 dummy 错误码
    pushq $2                    # 压入中断号 2
    SAVE_ALL_REGS

    # 🌟 修正：使用 64 位 testq 精确检测 Bit 63 是否为高半区内核地址
    rdgsbase %rbx
    testq %rbx, %rbx            # 检测全 64 位 GS_BASE 的符号位
    js 1f                       # 若符号位为 1 (内核高半区)，跳过 swapgs
    swapgs                      # 若为用户态低半区地址，切换到内核 GS
1:
    cld                         # 遵循 ABI 清除方向标志
    movq %rsp, %rdi
    call do_nmi

    # 根据进入时记录的状态决定是否切回用户 GS
    testq %rbx, %rbx
    js 2f
    swapgs
2:
    RESTORE_ALL_REGS
    addq $16, %rsp
    iretq