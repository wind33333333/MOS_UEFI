.global context_switch
.type context_switch, @function

// =========================================================================
// void context_switch(uint64 *prev_rsp, uint64 *next_rsp);
// 参数传递 (System V ABI):
// RDI = prev_rsp 的地址 (即 &prev->rsp)
// RSI = next_rsp 的地址 (即 &next->rsp)
// =========================================================================
context_switch:
    // 1. 保存当前任务 (prev) 的执行现场到 prev 的栈中
    pushfq              // 保存 RFLAGS (包含中断开关状态等标志位)
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    // 2. 🌟 灵魂出窍：保存旧 RSP，换上新 RSP！
    movq %rsp, (%rdi)   // 把当前压好栈的 RSP 存入 prev->rsp
    movq (%rsi), %rsp   // 从 next->rsp 读出新栈的指针，覆盖当前 RSP！

    // 3. 借尸还魂：从 next 的栈中恢复它曾经的执行现场
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    popfq               // 恢复 RFLAGS

    // 4. 返回
    // 此时的栈顶是啥？是 next 任务当初调用 context_switch 时压入的返回地址！
    // ret 指令会把这个地址弹给 RIP，CPU 完美继续执行 next 任务！
    ret