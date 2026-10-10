#include "../sched/task.h"
#include "slub.h"
#include "sched.h"
#include "sched_eevdf.h"

// 外部汇编函数声明
void ret_from_fork();


// 辅助函数：根据权重刷新逆乘数
static inline void calc_task_wmult(task_t *task) {
    uint64 w = task->weight ? task->weight : NICE_0_LOAD;
    task->w_mult = ((NICE_0_LOAD << WMULT_SHIFT) / w);
}

// 任务创建函数
task_t* create_kernel_task(void (*entry_point)(void), uint64 arg) {
    task_t *new_task = kmalloc(sizeof(task_t));
    void *stack = kmalloc(8192); // 分配一块内存作为栈

    // 1. 栈顶指针 (x86 栈是向下生长的，所以从高地址开始)
    uint64 *rsp = (uint64 *)((uint64)stack + 8192);

    // 2. 开始伪造案发现场 (顺序必须与 context_switch 里的 pop 严格逆序！)

    // (对应汇编最后的 ret 指令)
    *(--rsp) = (uint64)ret_from_fork;

    // 🌟 (对应汇编倒数第二条的 popfq 指令)
    // 0x202 代表默认开启 IF 中断标志位
    *(--rsp) = 0x202;

    // (对应汇编里的 6 个 popq 通用寄存器，逆序压入)
    *(--rsp) = 0; // rbp
    *(--rsp) = (uint64)entry_point; // rbx
    *(--rsp) = arg; // r12
    *(--rsp) = 0; // r13
    *(--rsp) = 0; // r14
    *(--rsp) = 0; // r15


    // 3. 记录这个精心伪造的栈顶地址
    new_task->rsp = (uint64)rsp;
    new_task->state = TASK_READY;

    // =======================================================
    // 🌟 EEVDF 引擎初始化
    // =======================================================
    new_task->weight = NICE_0_LOAD;            // 默认公平权重
    calc_task_wmult(new_task);                 // 🌟 计算初始 w_mult
    new_task->time_slice = 10000000ULL;        // 10ms 基础配额

    // 严厉打击“出生特权”：虚拟时间直接对齐当前系统 Vtime
    new_task->v_eligible = THIS_CPU->vtime;

    new_task->v_deadline = 0;                  // 留给 enqueue 时计算
    new_task->min_v_deadline = 0;              // 留给 enqueue 时计算
    new_task->last_update_time = 0;            // 留给 schedule 调度时记录
    // =======================================================

    new_task->wake_up_ns = 0;

    return new_task;
}

task_t* create_user_task(void *user_entry, void *user_stack) {
    task_t *new_task = kmalloc(sizeof(task_t));
    void *kstack = kmalloc(4096);
    uint64 *rsp = (uint64 *)((uint64)kstack + 4096);

    // 1. 中断返回现场 (Ring 3 iretq 现场)
    *(--rsp) = 0x23;                 // 用户态 SS
    *(--rsp) = (uint64)user_stack;   // 用户态 RSP
    *(--rsp) = 0x202;                // RFLAGS
    *(--rsp) = 0x1B;                 // 用户态 CS
    *(--rsp) = (uint64)user_entry;   // RIP

    *(--rsp) = 0; // err_code
    *(--rsp) = 0; // int_no
    *(--rsp) = 0; // rax
    *(--rsp) = 0; // rcx
    *(--rsp) = 0; // rdx
    *(--rsp) = 0; // rbx
    *(--rsp) = 0; // rsp (dummy)
    *(--rsp) = 0; // rbp
    *(--rsp) = 0; // rsi
    *(--rsp) = 0; // rdi
    *(--rsp) = 0; // r8
    *(--rsp) = 0; // r9
    *(--rsp) = 0; // r10
    *(--rsp) = 0; // r11
    *(--rsp) = 0; // r12
    *(--rsp) = 0; // r13
    *(--rsp) = 0; // r14
    *(--rsp) = 0; // r15

    // 2. 内核切栈现场
    *(--rsp) = (uint64)ret_from_fork;
    *(--rsp) = 0x202;
    *(--rsp) = 0; // RBP
    *(--rsp) = 0; // RBX = 0 标记用户态任务

    new_task->rsp = (uint64)rsp;
    new_task->state = TASK_READY;
    new_task->weight = NICE_0_LOAD;
    calc_task_wmult(new_task); // 🌟 计算初始 w_mult
    new_task->time_slice = 10000000ULL;
    new_task->v_eligible = THIS_CPU->vtime;
    new_task->v_deadline = 0;
    new_task->min_v_deadline = 0;
    new_task->last_update_time = 0;
    new_task->wake_up_ns = 0;

    return new_task;
}

/**
 * @brief 动态修改任务权重 (相当于 Linux 的 nice 命令)
 * @param task 目标任务指针
 * @param new_weight 新的权重 (NICE_0_LOAD 为基准，越大优先级越高)
 */
void set_task_weight(task_t *task, uint64 new_weight) {
    if (!task || new_weight == 0) return; // 防呆保护

    uint64 flags;
    local_irq_save(&flags);

    // 1. 判断任务当前是否物理存在于 EEVDF 调度树中
    int on_rq = (task->state == TASK_READY);

    // 2. 🌟 核心铁律：如果任务在树上，绝对不能直接改！必须先拔下来！
    // 否则直接改权重会导致 Vd 发生变化，破坏增强红黑树的 min_v_deadline 结构。
    if (on_rq) {
        dequeue_task_eevdf(task);
    }

    // 3. 安全修改物理数据
    task->weight = new_weight;
    calc_task_wmult(task); // 🌟 计算初始 w_mult

    // 4. 将任务重新种回树上
    // enqueue_task_eevdf 内部会根据新的 weight，重新计算 v_deadline，并安全触发 O(log N) 增强修复
    if (on_rq) {
        enqueue_task_eevdf(task);
    }

    // 5. 抢占裁决：如果你修改的是当前正在 CPU 上跑的任务
    // 比如它的权重被降低了，那我们有理由怀疑此时树上可能存在比它更渴望 CPU 的任务。
    // 贴上抢占便签，强制它在下次中断退出时交出麦克风，走一遍 EEVDF 的 pick_next 裁决。
    if (task == THIS_CPU->cur_task) {
        task->flags |= TIF_NEED_RESCHED;
    }

    local_irq_restore(flags);
}

/**
 * @brief 动态修改任务的基础时间片配额
 * @param task 目标任务指针
 * @param new_slice_ns 新的时间片 (纳秒)
 */
void set_task_time_slice(task_t *task, uint64 new_slice_ns) {
    if (!task || new_slice_ns == 0) return;

    uint64 flags;
    local_irq_save(&flags);

    int on_rq = (task->state == TASK_READY);

    // 拔树
    if (on_rq) {
        dequeue_task_eevdf(task);
    }

    // 修改时间片 (影响 Vd 计算的另一个核心变量)
    task->time_slice = new_slice_ns;

    // 种树 (重新演算 Vd 并寻找新位置)
    if (on_rq) {
        enqueue_task_eevdf(task);
    }

    if (task == THIS_CPU->cur_task) {
        task->flags |= TIF_NEED_RESCHED;
    }

    local_irq_restore(flags);
}


void idle_task(void) {
    task_t *idle_task = kzalloc(sizeof(task_t));
    idle_task->id = 1;
    idle_task->state = TASK_RUNNING;
    idle_task->flags = TIF_NEED_RESCHED;

    run_queue_t *run_queue = kmalloc(sizeof(run_queue_t));
    run_queue->idle_task = idle_task;
    run_queue->cur_task = idle_task;
    run_queue->vtime = 0;

    THIS_CPU->run_queue = run_queue;
    THIS_CPU->sleep_queue = kzalloc(sizeof(sleep_queue_t));

    while(TRUE) {
        // 1. 关中断：锁死物理大门，防止在判断期间有中断闯入！
        asm_cli();

        // 2. 检查是否有任务需要抢占
        if (THIS_CPU->run_queue->cur_task->flags & TIF_NEED_RESCHED) {
            // 如果有，赶紧开中断，并交出 CPU
            asm_sti();
            schedule();
        } else {
            // 3. 🌟 终极魔法：sti 和 hlt 的原子化结合！
            // 在 x86 架构中，sti 指令有一个神奇的特性：
            // 它不会立刻开启中断，而是会延迟一条指令生效！
            // 所以 sti 紧跟 hlt，CPU 会在进入 hlt 沉睡的同时，瞬间将大门打开。
            // 这样就绝对不会漏掉任何一个中断！
            asm_sti();
            asm_hlt();
        }
    }
}

