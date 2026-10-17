#include "cpu_registers.h"
#include "syscall_entry.h"

.code64
.section .text, "ax"

// ================= 系统调用汇编入口 ==========================

// 引用段选择子定义 (与 STAR_MSR 推导规则严格匹配：0x18+8=0x23, 0x18+16=0x2B)
.set USER_DS, 0x23                      // GDT 用户数据段 (RPL=3)
.set USER_CS, 0x2B                      // GDT 64位用户代码段 (RPL=3)

// Per-CPU 结构体内部偏移 (通过 %gs: 寻址)
.set PERCPU_KERNEL_STACK_TOP, 0x00      // 当前 CPU 的内核栈顶
.set PERCPU_USER_RSP,         0x08      // 暂存用户栈指针的临时槽位

.set ENOSYS, 38                         // -38 代表系统调用未实现

.extern syscall_table
.global syscall_entry
.p2align 4
syscall_entry:
    // ---------------------------------------------------------
    // 1. 换入内核 GS 与安全内核栈
    // ---------------------------------------------------------
    swapgs
    movq %rsp, %gs:PERCPU_USER_RSP          // 暂存不可信的用户 RSP
    movq %gs:PERCPU_KERNEL_STACK_TOP, %rsp  // 切换至当前 CPU 的内核栈顶

    // ---------------------------------------------------------
    // 2. 模拟构造硬件栈现场 (共 40 字节)
    // ---------------------------------------------------------
    pushq $USER_DS                          // 偏移 0xA8: ss
    pushq %gs:PERCPU_USER_RSP               // 偏移 0xA0: rsp
    pushq %r11                              // 偏移 0x98: rflags
    pushq $USER_CS                          // 偏移 0x90: cs
    pushq %rcx                              // 偏移 0x88: rip

    // ---------------------------------------------------------
    // 3. 构造元数据 (共 16 字节)
    // ---------------------------------------------------------
    pushq $0                                // 偏移 0x80: err_code = 0
    pushq %rax                              // 🌟 偏移 0x78: int_no / sys_no (备份原始系统调用号)

    // ---------------------------------------------------------
    // 4. 保存通用寄存器 (共 120 字节)
    // ---------------------------------------------------------
    SAVE_ALL_REGS
    // 此时栈顶 %rsp 严格对齐至 cpu_registers_t 起始位置 (r15)

    // ---------------------------------------------------------
    // 5. 汇编层直接查表分发
    // ---------------------------------------------------------
    cmpq $MAX_SYSCALL_NUM, %rax             // 检查系统调用号范围 (无符号比较)
    jae .Lsyscall_bad_num

    leaq syscall_table(%rip), %r11          // 取系统调用表基地址
    movq (%r11, %rax, 8), %r11              // R11 = syscall_table[RAX]
    testq %r11, %r11                        // 检查函数指针是否为 NULL
    jz .Lsyscall_bad_num

    movq %rsp, %rdi                         // 第 1 参数：cpu_registers_t *regs
    call *%r11                              // 直接调用 sys_xxx(regs)
    jmp .Lsyscall_done

.Lsyscall_bad_num:
    movq $-ENOSYS, %rax                     // 错误号：-38 (-ENOSYS)

.Lsyscall_done:
    // ---------------------------------------------------------
    // 6. 回写返回值到栈帧 RAX 槽位
    // ---------------------------------------------------------
    // 此时偏移 0x78 依然保留着原始调用号，而 0x70 被更新为返回值
    movq %rax, 0x70(%rsp)

    // ---------------------------------------------------------
    // 7. 恢复现场并极速返回 (sysretq)
    // ---------------------------------------------------------
    RESTORE_ALL_REGS                        // 恢复 R15 ~ RAX (RAX 拿到最新返回值)

    addq $16, %rsp                          // 跨过 sys_no 与 err_code

    popq %rcx                               // 恢复用户 RIP 到 RCX
    addq $8, %rsp                           // 跨过 CS
    popq %r11                               // 恢复用户 RFLAGS 到 R11
    popq %rsp                               // 恢复用户栈指针 RSP (切回用户栈)

    swapgs                                  // 换回用户 GS
    sysretq                                 // 硬件极速还阳