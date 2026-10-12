.global syscall_entry
.extern do_syscall_dispatcher

// 假定 Per-CPU 结构体 (gs 寄存器基地址) 偏移：
// 0x00: 内核栈顶 (kernel_stack_top)
// 0x08: 暂存用户栈顶 (user_rsp_temp)

syscall_entry:
    // 1. 原子切入内核 GS 段
    //swapgs

    // 2. 换栈：暂存用户栈指针，换上当前任务的内核栈
    movq %rsp, %gs:0x08           // 保存用户的 rsp 到 per-cpu scratch 槽
    movq %gs:0x00, %rsp           // 加载安全的内核栈顶

    // 3. 构造内核栈帧 (压入上下文)
    // 必须保存 RCX (用户 RIP) 和 R11 (用户 RFLAGS)，否则无法返回
    pushq %gs:0x08                // 压入用户原始 RSP
    pushq %r11                    // 压入用户原始 RFLAGS
    pushq %rcx                    // 压入用户原始 RIP

    // 压入其余通用寄存器 (Callee/Caller-saved)
    pushq %rbx
    pushq %rbp
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10                    // 🌟 注意：系统调用第 4 个参数在 R10
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    // 4. 参数对齐与 C 语言分发
    // System V AMD64 ABI 系统调用规约：
    // 用户态传参：RAX (调用号), RDI (arg1), RSI (arg2), RDX (arg3), R10 (arg4), R8 (arg5), R9 (arg6)
    // C 函数调用规约传参：RDI, RSI, RDX, RCX, R8, R9
    movq %r10, %rcx               // 将系统调用的第 4 参数 R10 挪给 C 调用的 RCX

    // (此时若要开启中断，可在此执行 sti)
    //sti

    // 调用 C 语言分发器：do_syscall_dispatcher(syscall_no, a1, a2, a3, a4, a5)
    // RDI 已经是 arg1，但我们需要把 RAX (调用号) 传进分发器
    movq %rax, %r12               // 备份 syscall 编号
    call do_syscall_dispatcher

    // 此时 RAX 保存了 C 函数的返回值

    // 5. 退出路径 (准备还阳)
    // (若此前执行了 sti，此处必须 cli 关中断再换栈)
    cli

    // 逆序恢复寄存器
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rbp
    popq %rbx

    // 恢复系统调用返回关键现场
    popq %rcx                     // 恢复用户态目标 RIP
    popq %r11                     // 恢复用户态目标 RFLAGS
    popq %rsp                     // 恢复用户态栈指针 RSP！

    // 6. 换回用户态 GS 并极速返回 Ring 3
    //swapgs
    sysretq                       // REX.W + sysret：硬件直接以 64 位原生模式返回 Ring 3！