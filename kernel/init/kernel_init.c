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
extern void context_switch(uint64 *prev_rsp, uint64 *next_rsp);

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
task_t* create_task(void (*entry_point)(void), uint64 stack_size) {
    task_t *task = kmalloc(sizeof(task_t));
    void *stack = kmalloc(stack_size); // 分配一块内存作为栈

    // 1. 栈顶指针 (x86 栈是向下生长的，所以从高地址开始)
    uint64 *sp = (uint64 *)((uint64)stack + stack_size);

    // 2. 开始伪造案发现场 (顺序必须与 context_switch 里的 pop 严格逆序！)

    // (对应汇编最后的 ret 指令)
    *(--sp) = (uint64)entry_point;

    // 🌟 (对应汇编倒数第二条的 popfq 指令)
    // 0x202 代表默认开启 IF 中断标志位
    *(--sp) = 0x202;

    // (对应汇编里的 6 个 popq 通用寄存器，逆序压入)
    *(--sp) = 0; // rbp
    *(--sp) = 0; // rbx
    *(--sp) = 0; // r12
    *(--sp) = 0; // r13
    *(--sp) = 0; // r14
    *(--sp) = 0; // r15


    // 3. 记录这个精心伪造的栈顶地址
    task->rsp = (uint64)sp;
    task->state = TASK_READY;

    return task;
}


task_t *task_a;
task_t *task_b;



void thread_a() {
    while(1) {
        PR_INFO("A ");
        uint64 times = 0xFFFF;
        while (times--) {
            asm_pause();
        }
    }
}

void thread_b() {
    while(1) {
        PR_INFO("B ");
        uint64 times = 0xFFFF;
        while (times--) {
            asm_pause();
        }
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
    task_a = create_task(thread_a, 4096);
    task_a->id = 1;
    enqueue_task(task_a);

    task_b = create_task(thread_b, 4096);
    task_b->id = 2;
    enqueue_task(task_b);

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
