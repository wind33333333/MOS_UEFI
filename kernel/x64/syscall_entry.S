  #include "cpu_registers.h"
  #include "syscall_entry.h"

  .code64
  .section .text, "ax"

  #================= 系统调用汇编入口 ==========================

    # 引用段选择子定义 (与 GDT 严格匹配)
    .set USER_CS, 0x1B      # 例如 GDT 索引 3，RPL=3 (0x18 | 3)
    .set USER_DS, 0x23      # 例如 GDT 索引 4，RPL=3 (0x20 | 3)

    # Per-CPU 结构体内部偏移 (通过 %gs: 寻址)
    .set PERCPU_KERNEL_STACK_TOP, 0x00    # 当前 CPU 的内核栈顶
    .set PERCPU_USER_RSP,   0x08          # 暂存用户栈指针的临时槽位

    .set ENOSYS, 38                     # -38 代表系统调用不存在

    .extern syscall_table
    .global syscall_entry
    .align 16
syscall_entry:
    # ---------------------------------------------------------
    # 1. 换入内核 GS 与安全内核栈
    # ---------------------------------------------------------
    swapgs
    movq %rsp, %gs:PERCPU_USER_RSP      # 暂存不可信的用户 RSP
    movq %gs:PERCPU_KERNEL_STACK_TOP, %rsp    # 切换至当前 CPU 的内核栈顶

    # ---------------------------------------------------------
    # 2. 模拟构造硬件栈现场 (共 40 字节)
    # ---------------------------------------------------------
    pushq $USER_DS
    pushq %gs:PERCPU_USER_RSP
    pushq %r11                          # 用户态 RFLAGS (硬件已存入 R11)
    pushq $USER_CS
    pushq %rcx                          # 用户态 RIP (硬件已存入 RCX)

    # ---------------------------------------------------------
    # 3. 构造元数据 (共 16 字节)
    # ---------------------------------------------------------
    pushq $0                            # err_code = 0
    pushq $0x80                         # int_no = 0x80 (系统调用标记)

    # ---------------------------------------------------------
    # 4. 保存通用寄存器 (共 120 字节)
    # ---------------------------------------------------------
    SAVE_ALL_REGS
    # 此时栈顶 %rsp 严格对齐至 cpu_registers_t 结构体的起始位置 (r15)

    # ---------------------------------------------------------
    # 5. 汇编层直接查表分发
    # ---------------------------------------------------------
    cmpq $MAX_SYSCALL_NUM, %rax         # 检查系统调用号范围
    jae .Lsyscall_bad_num

    leaq syscall_table(%rip), %r11      # 取系统调用表基地址
    movq (%r11, %rax, 8), %r11          # R11 = sys_call_table[RAX]
    testq %r11, %r11                    # 检查是否为 NULL
    jz .Lsyscall_bad_num

    # 🌟 统一调用规约：
    # System V ABI 第 1 个参数使用 %rdi。
    # 当前 %rsp 就是 cpu_registers_t 结构体的起始指针！
    movq %rsp, %rdi
    call *%r11                         # 执行 sys_xxx(regs)
    jmp .Lsyscall_done

.Lsyscall_bad_num:
    movq $-ENOSYS, %rax                 # 错误号：-38

.Lsyscall_done:
    # ---------------------------------------------------------
    # 6. 回写返回值
    # ---------------------------------------------------------
    # C 函数返回值存放在 %rax 中。
    # 将其回写到栈帧中的 RAX 槽位 (偏移 0x70 = 112 字节)。
    movq %rax, 0x70(%rsp)

    # ---------------------------------------------------------
    # 7. 恢复现场并极速返回 (sysretq)
    # ---------------------------------------------------------
    RESTORE_ALL_REGS                    # 恢复 R15 ~ RAX (RAX 取出最新返回值)

    addq $16, %rsp                      # 跨过 int_no 与 err_code

    popq %rcx                           # 恢复用户 RIP 到 RCX
    addq $8, %rsp                       # 跨过 CS
    popq %r11                           # 恢复用户 RFLAGS 到 R11
    popq %rsp                           # 恢复用户栈指针 RSP (切回用户栈)

    swapgs                              # 换回用户 GS
    sysretq                             # 硬件极速还阳
