.global ret_from_fork
.type ret_from_fork, @function

ret_from_fork:
    // 此时 context_switch 刚执行完 ret，跳到了这里。
    // CPU 状态：中断通常是关闭的，我们在 Ring 0
    // RBX = 内核线程的函数地址 (如果是用户进程，RBX = 0)
    // R12 = 传递给内核线程的参数

    // 1. 检查 RBX 是否为空？
    testq %rbx, %rbx
    jz 1f                   // 🌟 如果 RBX 是 0，说明是用户进程！跳到下面去执行 iretq！

    // 2. 如果 RBX 不是 0，说明是内核线程！
    // 准备参数并调用内核函数
    movq %r12, %rdi         // 根据 C 语言 ABI，第一个参数放在 RDI
    call *%rbx              // 直接 call 业务函数 (比如 thread_a)

    // 内核线程不应该 return，如果它不小心 return 了，必须强行销毁它
    //call do_exit            // 相当于线程自杀，永远不会返回
    jmp .

1:
    // ==========================================================
    // 🌟 用户进程的专属还阳通道
    // ==========================================================
    // 当代码走到这里时，说明 RBX=0 (用户态新兵)
    // 或者它是一个普通的旧任务刚刚经历了 check_and_schedule

    // 此时的栈顶 (%rsp)，必须刚好对齐到一个完整的 cpu_registers_t 结构！
    // 也就是中断刚发生时，压入的那 15 个通用寄存器 + 中断号 + iretq 需要的 5 个硬件寄存器

    # 恢复现场
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

    # 跨过我们在桩里手动压入的 int_no 和 err_code (共 16 字节)
    addq $16, %rsp

    // 终极降维打击，瞬间返回 Ring 3！
    iretq