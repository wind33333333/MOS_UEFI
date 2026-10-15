#include "../sched/sched.h"
#include "../sched/sched_eevdf.h"


void context_switch(uint64 *prev_rsp, uint64 *next_rsp);


/**
 * @brief 主调度器接管点
 */
void schedule() {
    uint64 flags;
    local_irq_save(&flags);

    uint64 cur_ns = get_uptime_ns();

    task_t *prev = THIS_CPU->cur_task;

    // 1. 结清前一个任务的时间账单
    update_cur_task_eevdf(cur_ns);
    prev->flags &= ~TIF_NEED_RESCHED; // 撕下便签

    // 2. 状态分发
    if (prev->state == TASK_RUNNING) {
        // 如果依然是 running，说明是时间片耗尽被强行切下的，重新入队继续排
        enqueue_task_eevdf(prev);
    }
    // 如果是 TASK_SLEEPING，不入队，它已经在定时器树里了

    // 3. 选出下一个天命之子
    task_t *next = pick_next_task_eevdf();
    if (!next) {
        next = THIS_CPU->idle_task;
    } else {
        dequeue_task_eevdf(next); // 离开就绪队列，独占 CPU
    }

    next->state = TASK_RUNNING;
    next->last_update_time = cur_ns;
    THIS_CPU->cur_task = next;

    // 🌟 1. 切换页表 (CR3)
    if (next->vm_space.cr3_root != 0 && next->vm_space.cr3_root != asm_get_cr3()) {
        asm_set_cr3(next->vm_space.cr3_root);
    }

    // 🌟 2. 刷新当前 CPU 核心的 TSS RSP0，防止 Ring 3 中断切栈时踩踏
    if (next->kernel_stack_top != 0) {
        THIS_CPU->tss->rsp0 = next->kernel_stack_top;
        THIS_CPU->current_kernel_stack = next->kernel_stack_top; // 供 syscall 汇编快速取用
    }

    // ========================================================
    // 🌟 终极修复：换人后，决不能继承旧闹钟！
    // 因为 next 刚刚出列，它的 v_deadline 是重新充满的 10ms，
    // 调用这个引擎，APIC 就会极其精准地被设定在 10ms 之后！
    // ========================================================
    reprogram_timer_for_next_event(cur_ns);

    // 4. 底层汇编硬切换
    if (prev != next) {
        context_switch(&prev->kernel_rsp, &next->kernel_rsp);
    }

    local_irq_restore(flags);
}




