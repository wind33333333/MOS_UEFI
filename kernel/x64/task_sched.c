#include "task_sched.h"
#include "slub.h"
#include "time.h"

task_t idle_task; //系统空闲任务，系统看门狗

runqueue_t g_rq; // 假设单核，多核则是 Per-CPU 变量

// 外部汇编函数声明
void context_switch(uint64 *prev_rsp, uint64 *next_rsp);
void ret_from_fork();

/**
 * @brief 核心计算：重新计算当前节点的子树最小 Vd
 */
static void eevdf_update_min_vd(task_t *t) {
    t->min_v_deadline = t->v_deadline; // 初始假设自己最小

    // 探测左子树
    if (t->sched_node.left) {
        task_t *l = CONTAINER_OF(t->sched_node.left, task_t, sched_node);
        if (l->min_v_deadline < t->min_v_deadline) {
            t->min_v_deadline = l->min_v_deadline;
        }
    }
    // 探测右子树
    if (t->sched_node.right) {
        task_t *r = CONTAINER_OF(t->sched_node.right, task_t, sched_node);
        if (r->min_v_deadline < t->min_v_deadline) {
            t->min_v_deadline = r->min_v_deadline;
        }
    }
}

/**
 * @brief [增强回调] 向上游传导更新 min_vd
 */
static void eevdf_augment_propagate(rb_node_t *node, rb_node_t *stop) {
    while (node != stop) {
        task_t *t = CONTAINER_OF(node, task_t, sched_node);
        uint64 old_min = t->min_v_deadline;

        eevdf_update_min_vd(t);

        // 🌟 极致优化：如果本节点的最小 Vd 没发生变化，上面的祖先绝对不会变，直接熔断！
        if (t->min_v_deadline == old_min) {
            break;
        }
        node = rb_parent(node);
    }
}

/**
 * @brief [增强回调] 节点被物理替换时，全盘继承数据
 */
static void eevdf_augment_copy(rb_node_t *old_node, rb_node_t *new_node) {
    task_t *old_t = CONTAINER_OF(old_node, task_t, sched_node);
    task_t *new_t = CONTAINER_OF(new_node, task_t, sched_node);
    new_t->min_v_deadline = old_t->min_v_deadline;
}

/**
 * @brief [增强回调] 红黑树旋转时，重新计算新老父节点的增强数据
 */
static void eevdf_augment_rotate(rb_node_t *old_node, rb_node_t *new_node) {
    task_t *old_t = CONTAINER_OF(old_node, task_t, sched_node);
    task_t *new_t = CONTAINER_OF(new_node, task_t, sched_node);

    // 必须先算被降级的旧节点，再算被提拔的新节点 (自底向上)
    eevdf_update_min_vd(old_t);
    eevdf_update_min_vd(new_t);
}

// 封装为回调结构体供 rb_insert 和 rb_erase 调用
rb_augment_callbacks_f eevdf_callbacks = {
    .propagate = eevdf_augment_propagate,
    .copy      = eevdf_augment_copy,
    .rotate    = eevdf_augment_rotate
};




/**
 * @brief 计费函数：推进当前任务的虚拟时间
 */
void update_curr(void) {
    task_t *curr = g_rq.current;
    if (!curr || curr == g_rq.idle_task) return;

    uint64 now = get_uptime_ns();
    uint64 delta_exec = now - curr->last_update_time;
    curr->last_update_time = now;

    // 计算消耗的虚拟时间: 物理时间 * (基准权重 / 自身权重)
    // 权重越大的任务，虚拟时间流逝越慢
    uint64 delta_vruntime = (delta_exec * NICE_0_LOAD) / curr->weight;

    // 因为是运行中，它的合格时间(Ve)随之推进
    curr->v_eligible += delta_vruntime;

    // 系统全局虚拟时间平滑追随当前任务
    if (curr->v_eligible > g_rq.vtime) {
        g_rq.vtime = curr->v_eligible;
    }
}

/**
 * @brief 将任务按 EEVDF 规则推入就绪树
 */
void enqueue_task_eevdf(task_t *task) {
    // 🌟 核心防线：idle_task 是兜底幽灵，绝对不能进排队名单！
    if (task == g_rq.idle_task) {
        task->state = TASK_READY;
        return; // 直接放行，不挂树！
    }

    // 1. 计算虚拟截止时间: Vd = Ve + (slice / weight)
    uint64 v_slice = (task->time_slice * NICE_0_LOAD) / task->weight;
    task->v_deadline = task->v_eligible + v_slice;

    // 初始增强数据
    task->min_v_deadline = task->v_deadline;

    // 2. 红黑树常规 O(log N) 挂载查找 (以 v_eligible 为 Key)
    rb_node_t **link = &g_rq.sched_tree.rb_node;
    rb_node_t *parent = NULL;

    while (*link) {
        parent = *link;
        task_t *parent_task = CONTAINER_OF(parent, task_t, sched_node);

        if (task->v_eligible < parent_task->v_eligible) {
            link = &parent->left;
        } else {
            link = &parent->right; // 相同 Ve 当作大于处理 (FIFO)
        }
    }

    // 3. 执行增强红黑树插入
    rb_insert(&g_rq.sched_tree, &task->sched_node, parent, link, &eevdf_callbacks);
    task->state = TASK_READY;
}

/**
 * @brief 从就绪树中拔出任务
 */
void dequeue_task_eevdf(task_t *task) {
    rb_erase(&g_rq.sched_tree, &task->sched_node, &eevdf_callbacks);
}

/**
 * @brief 🌟 EEVDF 核心裁决器：挑选最佳任务
 * @details 数学定义：在所有合格的任务 (Ve <= Vtime) 中，挑选截止时间 (Vd) 最小的。
 */
task_t* pick_next_task_eevdf(void) {
    rb_node_t *node = g_rq.sched_tree.rb_node;
    task_t *best = NULL;

    while (node) {
        task_t *curr = CONTAINER_OF(node, task_t, sched_node);
        task_t *left = curr->sched_node.left ? CONTAINER_OF(curr->sched_node.left, task_t, sched_node) : NULL;

        // 分支 A: 当前节点不合格 (Ve > Vtime)
        if (curr->v_eligible > g_rq.vtime) {
            // 右子树肯定更不合格(Ve更大)，唯一有希望的只在左子树
            node = curr->sched_node.left;
            continue;
        }

        // 分支 B: 当前节点合格 (Ve <= Vtime)
        // 说明 curr 及其整棵左子树全部合格！

        // 候选更新：当前节点可能是最优的
        if (!best || curr->v_deadline < best->v_deadline) {
            best = curr;
        }

        // 利用增强数据判断：左子树里是否藏着更紧急的任务 (Vd 更小)？
        if (left && (!best || left->min_v_deadline < best->v_deadline)) {
            node = curr->sched_node.left; // 左边有金矿，去左边挖
        } else {
            node = curr->sched_node.right; // 左边没有更好的了，去右边碰碰运气
        }
    }

    // 极端边缘保护：如果由于休眠等原因系统虚拟时间 Vtime 严重滞后，导致找不到合格任务
    if (!best) {
        rb_node_t *leftmost = rb_first(&g_rq.sched_tree);
        if (leftmost) {
            best = CONTAINER_OF(leftmost, task_t, sched_node);
            // 时间线快进 (Fast-forward)，强行让最左侧(Ve最小)的任务合格
            g_rq.vtime = best->v_eligible;
        }
    }

    return best;
}

/**
 * @brief 主调度器接管点
 */
void schedule(void) {
    uint64 flags;
    local_irq_save(&flags);

    task_t *prev = g_rq.current;

    // 1. 结清前一个任务的时间账单
    update_curr();
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
        next = g_rq.idle_task;
    } else {
        dequeue_task_eevdf(next); // 离开就绪队列，独占 CPU
    }

    next->state = TASK_RUNNING;
    next->last_update_time = get_uptime_ns();
    g_rq.current = next;

    // 4. 底层汇编硬切换
    if (prev != next) {
        context_switch(&prev->rsp, &next->rsp);
    }

    local_irq_restore(flags);
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
    new_task->time_slice = 10000000ULL;        // 10ms 基础配额

    // 严厉打击“出生特权”：虚拟时间直接对齐当前系统 Vtime
    new_task->v_eligible = g_rq.vtime;

    new_task->v_deadline = 0;                  // 留给 enqueue 时计算
    new_task->min_v_deadline = 0;              // 留给 enqueue 时计算
    new_task->last_update_time = 0;            // 留给 schedule 调度时记录
    // =======================================================

    new_task->wake_up_ns = 0;

    return new_task;
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

    // 4. 将任务重新种回树上
    // enqueue_task_eevdf 内部会根据新的 weight，重新计算 v_deadline，并安全触发 O(log N) 增强修复
    if (on_rq) {
        enqueue_task_eevdf(task);
    }

    // 5. 抢占裁决：如果你修改的是当前正在 CPU 上跑的任务
    // 比如它的权重被降低了，那我们有理由怀疑此时树上可能存在比它更渴望 CPU 的任务。
    // 贴上抢占便签，强制它在下次中断退出时交出麦克风，走一遍 EEVDF 的 pick_next 裁决。
    if (task == g_rq.current) {
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

    if (task == g_rq.current) {
        task->flags |= TIF_NEED_RESCHED;
    }

    local_irq_restore(flags);
}

void idle_task_init(void) {
    idle_task.id = 0;
    idle_task.state = TASK_RUNNING;
    g_rq.current = &idle_task;
    g_rq.idle_task = &idle_task;// 🌟 钦定自己为系统的 Idle Task

    // 4. 华丽转身：从“创世”进入“养老”循环
    while(1) {
        // 如果没人排队，schedule 会挑中我自己（idle_task）。
        // 切给自己 = 什么都没发生，直接 return，往下执行 hlt 节能。
        // 如果有人排队，schedule 会切给别人。等他们全睡了，又会切回这里。
        schedule();

        // 核心态停机指令，断电休眠，等待下一次时钟中断
        asm volatile("hlt");
    }
}


