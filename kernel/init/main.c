#include "../include/moslib.h"
#include "../include/printk.h"
#include "../include/ioapic.h"
#include "../include/buddy_system.h"
#include "../include/slub.h"
#include "../x64/cpu.h"
#include "../drivers/hpet/hpet.h"
#include "kernel_page_table.h"
#include "../include/memblock.h"
#include "../include/vmalloc.h"
#include "../include/rbtree.h"
#include "uefi.h"
#include "../include/bus.h"
#include "../x64/interrupt.h"
#include "../x64/apic.h"

static uint64 m = 0;

irqreturn_e tsc_isr(cpu_registers_t *regs, void *dev_id) {
    uint64 cur_tsc = asm_rdtsc_end(0);
    uint64 next_tsc = cpu_info.tsc_hz + cur_tsc;
    asm_wrmsr(IA32_TSC_DEADLINE,next_tsc);
    color_printk(RED,BLACK,"%ds ",++m);
}

INIT_TEXT void init_kernel(void) {
    asm_mem_set(_start_bss,0x0,_end_bss-_start_bss);    //初始化bss段
    enable_cpu_advanced_features();            //启用cpu开启高级功能
    init_output();                             //初始化输出控制台
    init_memblock();                           //初始化启动内存分配器
    init_kpage_table();                        //初始化正式内核页表
    init_buddy_system();                       //初始化伙伴系统
    init_slub();                               //初始化slub内存分配器
    init_rbtree_empty_augment_callbacks();     //初始化红黑树空回调函数
    init_vmalloc();                            //初始化vmalloc
    video_mem_map();                           //映射显存到虚拟地址空间
    efi_runtime_service_map();                 //映射efi运行时服务到虚拟地址空间
    init_ioapic();                             //初始化ioapic
    init_hpet();                               //初始化hpet
    init_bsp();                                //初始化bsp核心


    uint32 tsc_isr_num = alloc_irq();
    register_isr(tsc_isr_num,tsc_isr,NULL,"tsc-isr");
    enable_apic_time(cpu_info.tsc_hz,APIC_TSC_DEADLINE,tsc_isr_num);

    uint64 tsc, tsc_deadline;

    // 🌟 核心破局点：必须先告诉硬件“我要开启 TSC-Deadline 模式”！
    // 假设你处于 x2APIC 模式，Timer LVT 的 MSR 是 0x832
    // 0x40000 代表 Bit 18=1, Bit 17=0 (TSC-Deadline 模式)
    // 0x20 是你随便分配的一个测试中断向量号
    asm_wrmsr(0x832, 0x40020);

    // 为了防止时间太短导致定时器瞬间爆炸清零，我们加一个超级大的值 (约1.3秒)
    // 0x100000 大约只有 0.3 毫秒，在虚拟机里可能一个 VM-Exit 就耗光了！
    uint64 HUGE_DELAY = 0x100000000ULL;

    while (1) {
        // 1. 获取当前时间并加上巨大的偏移量
        tsc = asm_rdtsc_end(0) + HUGE_DELAY;

        // 2. 写入死线
        asm_wrmsr(IA32_TSC_DEADLINE, tsc);

        // 3. 立即读出验证！
        tsc_deadline = asm_rdmsr(IA32_TSC_DEADLINE);

        // 打印看看！这时候绝对不可能再是 0 了，它一定会完美等于你写入的 tsc 值！
        color_printk(GREEN, BLACK, "Written: %#lx, Read: %#lx\n", tsc, tsc_deadline);

        // 死循环卡住，不要让它狂刷
        while(1);
    }
    while (1);
    bus_init();                                //总线初始化
    init_ap();                                 //初始化ap核

    //ENABLE_HPET_TIMES(*hpetRegisters.tim0_conf,*hpetRegisters.tim0_comp,0x3000000,HPET_PERIODIC,0);
    //enable_apic_time(0xF000,APIC_TSC_DEADLINE,0x20);

    //STI();
    while (1);
}
