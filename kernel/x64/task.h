#pragma once
#include "moslib.h"
#include "rbtree.h"

// 任务状态枚举
typedef enum {
    TASK_READY,   // 在就绪队列，渴望 CPU
    TASK_RUNNING, // 正在执行
    TASK_SLEEPING // 在睡眠树/堆中，等待绝对时间唤醒
} task_state_t;


// 任务控制块 (TCB)
typedef struct task_t {
    uint64 rsp;                 // 🌟 栈指针 (必须在结构体最开头，方便汇编存取)
    uint64 id;                  // 任务 ID
    task_state_t state;
    uint32 priority;            // 优先级
    uint8 need_resched;

    // 🌟 新增：睡眠/定时器专用字段
    uint64 wake_up_ns;        // 红黑树的 Key (绝对纳秒时间)
    rb_node_t sleep_node;     // 挂入睡眠树的红黑树节点
    struct task_t *next;   // 就绪队列链表指针
    // ... 其他信息 (页表 CR3, 内存空间等) ...
} task_t;

task_t* create_kernel_task(void (*entry_point)(void), uint64 arg) ;

task_t* create_user_task(void *user_entry, void *user_stack);

// 调度器全局/局部数据
typedef struct {
    task_t *current;      // 当前正在 CPU 上跑的任务
    task_t *ready_head;   // 就绪队列头
    task_t *ready_tail;   // 就绪队列尾
    task_t *idle_task;
    rb_root_t sleep_tree;    // 🌟 新增：睡眠红黑树 (按 wake_up_ns 从小到大排序)
} runqueue_t;

// 任务入队列
void enqueue_task(task_t *task);

task_t* dequeue_task();

void idle_task_init(void);

