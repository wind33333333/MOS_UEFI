#include "../include/moslib.h"
#include "memblock_init.h"
#include "vmm_init.h"
#include "pmm_init.h"
#include "slub_init.h"
#include "cpu_init.h"
#include "kpt_init.h"
#include "vmalloc_init.h"
#include "uefi_init.h"
#include "alternative_init.h"
#include "video_init.h"
#include "apic_init.h"
#include "printk.h"
#include "../x64/mtrr.h"
#include "../x64/apic_time.h"
#include "../include/bus.h"
#include "../x64/interrupt.h"
#include "../include/ioapic.h"
#include "../drivers/hpet/hpet.h"
#include "../time/time_core.h"
#include "slub.h"


runqueue_t g_rq; // 假设单核，多核则是 Per-CPU 变量

// 基础队列操作
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

task_t* dequeue_task() {
    task_t *task = g_rq.ready_head;
    if (task) {
        g_rq.ready_head = task->next;
        if (g_rq.ready_head == NULL) g_rq.ready_tail = NULL;
    }
    return task;
}

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
    if (g_rq.current->need_resched) {
        g_rq.current->need_resched = 0; // 撕掉条子

        // 🌟 在这里进行真正的上下文切换！
        // 即使栈在这里被劫持，APIC 也绝对不会死锁，因为 EOI 早就发完了！
        schedule();
    }
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

/**
 * @brief 查找红黑树中 Key 最小的节点（最左侧节点）
 * @param root 树的根节点指针
 * @return 最小节点的指针，如果树为空则返回 NULL
 */
rb_node_t* rb_first(const rb_root_t *root) {
    rb_node_t *node = root->rb_node;

    // 如果树是空的，直接返回 NULL
    if (!node) {
        return NULL;
    }

    // 只要有左孩子，就一直往左走
    while (node->left) {
        node = node->left;
    }

    return node;
}

void reprogram_clockevent(void);

void sleep_us(uint64 delay_us) {
    uint64 flags;
    local_irq_save(&flags); // 关中断保护

    task_t *curr = g_rq.current;

    // 1. 记账：算好个人的醒来时间，并把自己的状态改为“睡觉”
    uint64 wake_ns = get_uptime_ns() + (delay_us * 1000ULL);
    curr->wake_up_ns = wake_ns;
    curr->state = TASK_SLEEPING;

    // 2. 存入账本：把当前任务挂入红黑树
    // (这里的 rb_insert 是你优化过的那套 O(1) NULL 判断的安全代码)
    rb_node_t **link = &g_rq.sleep_tree.rb_node; // 指向当前需要比较的节点指针
    rb_node_t *parent = NULL;                    // 记录未来新节点的父亲

    // 从根节点开始向下遍历，直到遇到空指针（即找到了叶子挂载点）
    while (*link) {
        parent = *link;
        // 通过树节点指针，反向获取到它所属的 task_t 结构体
        // 假设你定义了 rb_entry，类似于 Linux 的 rb_entry 宏
        task_t *parent_task = CONTAINER_OF(parent, task_t, sleep_node);

        // 比较唤醒时间 (Key)
        if (curr->wake_up_ns < parent_task->wake_up_ns) {
            link = &(*link)->left;  // 比父节点小，去左子树找
        } else {
            link = &(*link)->right; // 大于或等于父节点，去右子树找
        }
    }

    rb_insert(&g_rq.sleep_tree, &curr->sleep_node, parent, link, NULL);

    // 3. 🌟 对接你的漂亮硬件抽象！
    // 检查账本：我刚刚加进去的这个任务，是不是全系统最早要醒的？
    if (rb_first(&g_rq.sleep_tree) == &curr->sleep_node) {
        // 如果是，马上更新 CPU 的全局记录，并通知底层硬件改闹钟！
        THIS_CPU->next_deadline_ns = wake_ns;
        reprogram_clockevent();
    }

    // 4. 强制交出麦克风！
    // 不管别人死活，我自己要睡了，调用 schedule 切给下一个人！
    schedule();

    // --- (漫长的等待...) ---
    // 5. 等未来某一天时钟中断触发，Timer ISR 把你从红黑树里摘下来，
    // schedule() 才会返回，代码才会走到这里！
    local_irq_restore(flags);
}

//毫秒定时器
static inline sleep_ms (uint64 delay_ms) {
    sleep_us(delay_ms*1000);
}


/**
 * @brief 扫描睡眠树，唤醒所有到期的任务，并重设下一个硬件闹钟
 */
#define EARLY_WAKEUP_TOLERANCE_NS 2000ULL // 容差窗口：2微秒
void check_and_wakeup_sleeping_tasks(void) {
    // 1. 获取挂钟时间
    uint64 now_ns = get_uptime_ns();

    // 🌟 将容差直接加在当前时间上，形成“有效当前时间”
    // 在这个时间线之前的所有任务，一律统统叫醒！
    uint64 effective_now = now_ns + EARLY_WAKEUP_TOLERANCE_NS;

    rb_node_t *node;

    // 2. 循环检查睡眠红黑树
    while ((node = rb_first(&g_rq.sleep_tree)) != NULL) {
        task_t *sleep_task = CONTAINER_OF(node, task_t, sleep_node);

        // 如果连最左侧（最早）的人，唤醒时间都大于 effective_now
        // 说明所有人都还没睡够，直接停止捞人！
        if (sleep_task->wake_up_ns > effective_now) {
            break;
        }

        // =======================================================
        // 时间到了，捞人！
        // =======================================================
        rb_erase(&g_rq.sleep_tree, &sleep_task->sleep_node, NULL);
        sleep_task->state = TASK_READY;
        enqueue_task(sleep_task);

        if (sleep_task != g_rq.current) {
            g_rq.current->need_resched = 1; // 贴上换人标签
        }
    }

    // =======================================================
    // 3. 终极魔法：Tickless 续杯 / APIC 接力 (Relay)
    // =======================================================
    // 刚才的循环可能捞出了几个人，也可能一个都没捞出来（比如 APIC 中途溢出早醒）。
    // 不管怎样，我们再看一眼树上还有没有人在睡：
    node = rb_first(&g_rq.sleep_tree);
    if (node != NULL) {
        task_t *next_sleep_task = CONTAINER_OF(node, task_t, sleep_node);

        THIS_CPU->next_deadline_ns = next_sleep_task->wake_up_ns;
        reprogram_clockevent();
    } else {
        // 所有人都醒了，把硬件闹钟关掉，或者设成最大值
        THIS_CPU->next_deadline_ns = 0xFFFFFFFFFFFFFFFFULL;
        reprogram_clockevent();
    }
}


task_t *task_a;
task_t *task_b;
task_t *task_c;



void thread_a() {
    while(1) {
        color_printk(GREEN,BLACK,"A ");
        sleep_ms(1000);
    }
}

void thread_b() {
    while(1) {
        color_printk(RED,BLACK,"B ");
        sleep_ms(1000);
    }
}

void thread_c() {
    while(1) {
        color_printk(YELLOW,BLACK,"C ");
        sleep_ms(1000);
    }
}

task_t idle_task;

void kernel_init(void) {
    asm_mem_set(_start_bss,0x0,_end_bss-_start_bss);    //初始化bss段
    output_init();                                              //初始化输出控制台
    tmp_idt_init();                                             //初始化临时中断描述符表
    cpu_enable_feature();                                       //cpu启用高级特性
    apply_alternatives();                                       //动态修复指令
    vm_layout_init();                                           //虚拟内存空间初始化
    memblock_init();                                            //初始化启动内存分配器
    kpage_table_init();                                         //初始化正式内核页表
    apic_table_init();                                          //apic表初始化
    buddy_system_init();                                        //初始化伙伴系统
    slub_init();                                                //初始化slub内存分配器
    vmalloc_init();                                             //初始化vmalloc
    video_mem_map();                                            //映射显存到虚拟地址空间
    ioapic_init();                                              //初始化ioapic
    time_core_init();                                           //时钟系统初始化
    hpet_init();                                                //hpet初始化
    bsp_backup_mtrr_state();                                    //备份mtrr
    cpu_alloc_resources();                                      //给所有cpu分配资源
    apic_time_init();                                           //apic时钟定时器初始化
    cpu_load_resource();                                        //加载cpu资源
    efi_runtime_service_init();                                 //映射efi运行时服务到虚拟地址空间

    // 1. 初始化自己
    idle_task.id = 0;
    idle_task.state = TASK_RUNNING;
    g_rq.current = &idle_task;
    // 🌟 钦定自己为系统的 Idle Task
    g_rq.idle_task = &idle_task;

    // 1. 创建两个新任务
    task_a = create_kernel_task(thread_a, 0);
    task_a->id = 1;
    enqueue_task(task_a);

    task_b = create_kernel_task(thread_b, 0);
    task_b->id = 2;
    enqueue_task(task_b);

    task_c = create_kernel_task(thread_c, 0);
    task_c->id = 3;
    enqueue_task(task_c);

    // 4. 华丽转身：从“创世”进入“养老”循环
    while(1) {
        // 如果没人排队，schedule 会挑中我自己（idle_task）。
        // 切给自己 = 什么都没发生，直接 return，往下执行 hlt 节能。
        // 如果有人排队，schedule 会切给别人。等他们全睡了，又会切回这里。
        schedule();

        // 核心态停机指令，断电休眠，等待下一次时钟中断
        asm volatile("hlt");
    }

    bus_init();                                                 //总线初始化
    ap_init();                                                  //初始化ap核

    while (1);
}
