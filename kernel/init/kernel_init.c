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
#include "../x64/task_sched.h"
#include "../x64/timer.h"

task_t *task_a;
task_t *task_b;
task_t *task_c;
task_t *task_d;


// 全局纯内存计数器
volatile uint64 count_a = 0;
volatile uint64 count_b = 0;
volatile uint64 count_c = 0;

void thread_a() {
    while (1) {
        color_printk(GREEN, BLACK, "A ");
        uint64 i = 0xFF;
        while(i--) { asm_pause(); }
    }
}
void thread_b() {
    while (1) {
        color_printk(YELLOW, BLACK, "B ");
        uint64 i = 0xFF;
        while(i--) { asm_pause(); }
    }
}
void thread_c() {
    while (1) {
        color_printk(RED, BLACK, "C ");
        uint64 i = 0xFF;
        while(i--) { asm_pause(); }
    }
}

// 监控任务：绝对的裁判
void thread_monitor() {
    while(1) {
        // 睡 1 秒钟 (调用你写好的高精度红黑树休眠)
        sleep_ms(1000);

        // 醒来后，打印三个人的“业绩”
        color_printk(WHITE, BLACK, "1 Sec Report -> A: %ld, B: %ld, C: %ld\n",
                     count_a, count_b, count_c);
    }
}


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


    // 1. 创建两个新任务
    task_a = create_kernel_task(thread_a, 0);
    task_a->id = 1;
    enqueue_task_eevdf(task_a);

    task_b = create_kernel_task(thread_b, 0);
    task_b->id = 2;
    enqueue_task_eevdf(task_b);

    task_c = create_kernel_task(thread_c, 0);
    task_c->id = 3;
    enqueue_task_eevdf(task_c);

    // task_d = create_kernel_task(thread_monitor, 0);
    // task_c->id = 4;
    // enqueue_task_eevdf(task_d);

    idle_task_init();

    bus_init();                                                 //总线初始化
    ap_init();                                                  //初始化ap核

    while (1);
}
