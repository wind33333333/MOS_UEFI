#include "ap.h"

//多核处理器初始化
__attribute__((section(".init_text"))) void init_ap(UINT32 cpu_id,UINT8 bsp_flags) {
    if (bsp_flags) {
        color_printk(GREEN, BLACK, "CPU Manufacturer: %s  Model: %s\n",cpu_info_t.manufacturer_name, cpu_info_t.model_name);
        color_printk(GREEN, BLACK, "CPU Cores: %d  FundamentalFrequency: %ldMhz  MaximumFrequency: %ldMhz  BusFrequency: %ldMhz  TSCFrequency: %ldhz\n",cpu_info_t.cores_num,cpu_info_t.fundamental_frequency,cpu_info_t.maximum_frequency,cpu_info_t.bus_frequency,cpu_info_t.tsc_frequency);

        __asm__ __volatile__ (
                "xor %%rdx,	%%rdx	\n\t"
                "mov $0xC4500,%%rax	\n\t"   //bit8-10投递模式init101 ，bit14 1 ，bit18-19投递目标11所有处理器（不包括自身）
                "mov $0x830,	%%rcx	\n\t"    //INIT IPI
                "wrmsr	\n\t"
                "mov $0x5000,%%rcx	\n\t"       //延时
                "1:\tloop 1b	\n\t"
                "mov $0x830,	%%rcx	\n\t"
                "mov $0xC4610,%%rax	\n\t"   //Start-up IPI //bit0-7处理器启动地址000VV000的中间两位 ，bit8-10投递模式start-up110 ，bit14 1 ，bit18-19投递目标11所有处理器（不包括自身）
                "wrmsr	\n\t"
                "mov $0x830,	%%rcx	\n\t"
                "mov $0x50000,%%rcx	\n\t"       //延时
                "2:\tloop 2b	\n\t"
                "wrmsr	\n\t"
                :: :"%rax", "%rcx", "%rdx");
    }

    color_printk(GREEN, BLACK, "CPU%d init successful\n", cpu_id);

    init_cpu_num++;

    while(1){
        if(init_cpu_num == cpu_info_t.cores_num)
            break;
    }

    return;
}