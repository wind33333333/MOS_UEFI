#include "task_sched.h"
#include "slub.h"

runqueue_t g_rq; // 假设单核，多核则是 Per-CPU 变量

// 外部汇编函数声明
void context_switch(uint64 *prev_rsp, uint64 *next_rsp);
void ret_from_fork();

void schedule(void) {
    task_t *prev = g_rq.current; // 此时 prev 绝对不可能为 NULL

    // 如果上一任还能跑，重新排队
    if (prev->state == TASK_RUNNING && prev != g_rq.idle_task) {
        enqueue_task(prev);
    }

    // 从队列头部拿任务
    task_t *next = dequeue_task();

    // 🌟 核心兜底：如果没有就绪任务了？
    if (next == NULL) {
        // 切给空闲任务去休眠！
        next = g_rq.idle_task;
    }

    // 正常切换...
    if (prev != next) {
        next->state = TASK_RUNNING;
        g_rq.current = next;
        context_switch(&prev->rsp, &next->rsp);
    }
}

void check_and_schedule() {
    // 如果当前任务被贴了“换人”的条子
    if (g_rq.current->flags & TIF_NEED_RESCHED) {
        g_rq.current->flags &= ~TIF_NEED_RESCHED; // 撕掉条子

        // 🌟 在这里进行真正的上下文切换！
        // 即使栈在这里被劫持，APIC 也绝对不会死锁，因为 EOI 早就发完了！
        schedule();
    }
}


// 任务入队列
void enqueue_task(task_t *task) {
    task->next = NULL;
    if (g_rq.ready_tail) {
        g_rq.ready_tail->next = task;
    } else {
        g_rq.ready_head = task;
    }
    g_rq.ready_tail = task;
    task->state = TASK_READY;
}

//任务出队列
task_t* dequeue_task() {
    task_t *task = g_rq.ready_head;
    if (task) {
        g_rq.ready_head = task->next;
        if (g_rq.ready_head == NULL) g_rq.ready_tail = NULL;
    }
    return task;
}

// 任务创建函数
task_t* create_kernel_task(void (*entry_point)(void), uint64 arg) {
    task_t *task = kmalloc(sizeof(task_t));
    void *stack = kmalloc(8192); // 分配一块内存作为栈

    // 1. 栈顶指针 (x86 栈是向下生长的，所以从高地址开始)
    uint64 *sp = (uint64 *)((uint64)stack + 8192);

    // 2. 开始伪造案发现场 (顺序必须与 context_switch 里的 pop 严格逆序！)

    // (对应汇编最后的 ret 指令)
    *(--sp) = (uint64)ret_from_fork;

    // 🌟 (对应汇编倒数第二条的 popfq 指令)
    // 0x202 代表默认开启 IF 中断标志位
    *(--sp) = 0x202;

    // (对应汇编里的 6 个 popq 通用寄存器，逆序压入)
    *(--sp) = 0; // rbp
    *(--sp) = (uint64)entry_point; // rbx
    *(--sp) = arg; // r12
    *(--sp) = 0; // r13
    *(--sp) = 0; // r14
    *(--sp) = 0; // r15


    // 3. 记录这个精心伪造的栈顶地址
    task->rsp = (uint64)sp;
    task->state = TASK_READY;

    return task;
}

task_t* create_user_task(void *user_entry, void *user_stack) {
    task_t *task = kmalloc(sizeof(task_t));
    void *kstack = kmalloc(4096);
    uint64 *sp = (uint64 *)((uint64)kstack + 4096);

    // ==========================================================
    // 1. 伪造【中断返回现场】 (为了给最后的 iretq 使用)
    // ==========================================================
    *(--sp) = 0x23;                 // 用户态 SS (Ring 3 数据段)
    *(--sp) = (uint64)user_stack;   // 用户态 RSP (Ring 3 栈顶)
    *(--sp) = 0x202;                // RFLAGS (开启中断)
    *(--sp) = 0x1B;                 // 用户态 CS (Ring 3 代码段)
    *(--sp) = (uint64)user_entry;   // 🌟 用户态程序的真实入口地址！

    // 伪造 15 个通用寄存器 (初始全部清零)
    *(--sp) = 0; // err_code
    *(--sp) = 0; // int_no
    *(--sp) = 0; // rax
    // ... (省略压入其他 14 个寄存器 0) ...
    *(--sp) = 0; // r15

    // ==========================================================
    // 2. 伪造【context_switch 现场】 (盖在中断现场的上面)
    // ==========================================================
    *(--sp) = (uint64)ret_from_fork; // 🌟 统一跳板！
    *(--sp) = 0x202;                 // RFLAGS
    *(--sp) = 0;                     // RBP
    *(--sp) = 0;                     // 🌟 RBX 填 0！极其重要，这告诉跳板：“我是用户态任务”
    // ... 其他寄存器填 0

    task->rsp = (uint64)sp;
    task->state = TASK_READY;
    return task;
}


