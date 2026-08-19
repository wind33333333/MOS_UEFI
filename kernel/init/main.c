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
#include "../x64/mtrr.h"

static uint64 m = 0;

uint64 apic_hz;

irqreturn_e apic_isr(cpu_registers_t *regs, void *dev_id) {
    asm_wrmsr(APIC_INITIAL_COUNT_MSR, apic_hz);
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

    typedef struct {
        uint64 base_address;
        uint64 mask;
    }mtrr_addr_map_t;

    mtrr_addr_map_t mtrr_addr_map[10] = {0};

    uint64 mtrr_cap = asm_rdmsr(MSR_MTRRcap);
    uint8 vcnt = mtrr_cap&0xFF;
    uint64 mtrr_type = asm_rdmsr(MSR_MTRRdefType);
    color_printk(RED,BLACK,"mtrr_cap:%#x mtrr_type:%#x \n",mtrr_cap,mtrr_type);
    for (uint8 i = 0; i < vcnt; i++) {
        mtrr_addr_map[i].base_address = asm_rdmsr(MSR_MTRRphysBase(i));
        mtrr_addr_map[i].mask = asm_rdmsr(MSR_MTRRphysMask(i));
        color_printk(RED,BLACK,"mtrr[%d] phy_base:%#lx phy_mask:%#lx \n",i,mtrr_addr_map[i].base_address,mtrr_addr_map[i].mask);
    }


    // =================================================================
    // 探测 CPU 是否支持 Invariant TSC (永不停止的绝对时钟)
    // =================================================================
    {
        uint32 eax, ebx, ecx, edx;
        uint32 max_ext_leaf;

        // 1. 首先，必须检查 CPU 是否支持 "扩展 CPUID 叶子"
        // 发送 0x80000000，EAX 会返回支持的最大扩展功能号
        asm_cpuid(0x80000000, &max_ext_leaf, &ebx, &ecx, &edx);

        // 如果最大叶子号连 0x80000007 都不到，说明 CPU 极其古老，肯定不支持
        if (max_ext_leaf < 0x80000007) {
            color_printk(RED,BLACK,"no 0x80000007");
        }

        // 2. 查询高级电源管理信息 (Advanced Power Management)
        asm_cpuid(0x80000007, &eax, &ebx, &ecx, &edx);

        // 3. 检查 EDX 的第 8 位 (Bit 8: Invariant TSC)
        if (edx & (1 << 8)) {
            color_printk(GREEN,BLACK,"Invariant TSC");
        } else {
            color_printk(RED,BLACK,"no Invariant TSC");
        }
    }

    while (1);

    apic_hz = hpet_calibrate_apic_hz(&hpet_dev,10);
    color_printk(RED,BLACK,"apic_time_hz %d ",apic_hz);

    uint32 tsc_isr_num = alloc_irq();
    register_isr(tsc_isr_num,apic_isr,NULL,"tsc-isr");
    enable_apic_time(apic_hz,APIC_ONESHOT,tsc_isr_num);

    while (1);

    bus_init();                                //总线初始化
    init_ap();                                 //初始化ap核

    //ENABLE_HPET_TIMES(*hpetRegisters.tim0_conf,*hpetRegisters.tim0_comp,0x3000000,HPET_PERIODIC,0);
    //enable_apic_time(0xF000,APIC_TSC_DEADLINE,0x20);

    //STI();
    while (1);
}
