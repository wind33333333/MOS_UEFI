#pragma once
#include "moslib.h"
#include "rbtree.h"
#include "../time/time_core.h"

#define NICE_0_LOAD 1024ULL // 默认权重，等同于 Linux 的 nice 0

typedef enum {
    TASK_READY,    // 就绪态：任务已准备好，等待调度器分配CPU
    TASK_RUNNING,  // 运行态：任务正在CPU上执行
    TASK_SLEEPING, // 睡眠态：任务因等待某事件（如定时器、I/O）而主动放弃CPU
    TASK_DEAD      // 死亡态：任务已完成执行或被终止，等待回收资源
} task_state_t;

// 任务的异步标志位 (位图)
#define TIF_NEED_RESCHED  (1 << 0)  // 第 0 位：需要重新调度
#define TIF_SIGPENDING    (1 << 1)  // 第 1 位：有未处理的信号 (未来杀进程用)
#define TIF_SYSCALL_TRACE (1 << 2)  // 第 2 位：系统调用追踪 (未来调试用)

// 任务控制块 (TCB)
typedef struct task_t {
    uint64 rsp;                 // 🌟 栈指针 (必须在结构体最开头，方便汇编存取)
    uint64 id;                  // 任务 ID
    task_state_t state;
    uint32 priority;            // 优先级
    uint32 flags;               // 🌟 异步标志位图 (别人给我贴的便签)

    // =========================================================
    // 🌟 EEVDF 调度引擎专属核心字段
    // =========================================================
    uint64 weight;              // 任务权重 (优先级越高，权重越大)
    uint64 time_slice;          // 基础时间片配额 (物理纳秒，例如 10ms)

    uint64 v_eligible;          // 虚拟合格时间 (Ve = Vruntime - Lag)，作为红黑树的 Key
    uint64 v_deadline;          // 虚拟截止时间 (Vd = Ve + slice/weight)

    uint64 min_v_deadline;      // [增强红黑树] 记录自己及左右子树中最小的 Vd

    uint64 last_update_time;    // 上次物理更新的绝对纳秒时间

    // 🌟 新增：睡眠/定时器专用字段
    uint64 wake_up_ns;        // 红黑树的 Key (绝对纳秒时间)
    union {
        rb_node_t sched_node;   // 当 state == TASK_READY 时使用
        rb_node_t sleep_node;   // 当 state == TASK_SLEEPING 时使用
    };
    // ... 其他信息 (页表 CR3, 内存空间等) ...
} task_t;


// 调度队列 (Per-CPU)
typedef struct {
    task_t *cur_task;            // 当前正在 CPU 上飞驰的任务
    task_t *idle_task;          // 兜底的系统空闲任务 (hlt)

    // 🌟 新增：属于本队列的绝对物理时间快照
    uint64 clock;
    boolean skip_clock_update;


    // =========================================================
    // 🌟 EEVDF 就绪子系统 (活人区)
    // =========================================================
    uint64 vtime;               // 系统当前的全局虚拟时间 (V)
    rb_root_t sched_tree;       // EEVDF 增强红黑树 (以 Ve 为 Key)

    // =========================================================
    // 🌟 高精度定时子系统 (睡觉区)
    // =========================================================
    rb_root_t sleep_tree;       // 睡眠红黑树 (以 wake_up_ns 为 Key)

} runqueue_t;

extern runqueue_t g_rq;
void schedule(void);
void check_and_schedule();
task_t* create_kernel_task(void (*entry_point)(void), uint64 arg) ;
task_t* create_user_task(void *user_entry, void *user_stack);

// 任务入队列
void enqueue_task_eevdf(task_t *task);
void enqueue_task_eevdf(task_t *task);
void idle_task_init(void);

void update_curr();

static inline void update_rq_clock(void) {
    // 如果持有通行证，直接白嫖上一次的快照！
    if (g_rq.skip_clock_update) return;
    g_rq.clock = get_uptime_ns();
}

