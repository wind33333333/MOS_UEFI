#include "ap.h"
#include "printk.h"
#include "cpu.h"

__attribute__((section(".init.data"))) UINT32 init_cpu_num = 0;

//多核处理器初始化
__attribute__((section(".init_text"))) void init_ap(UINT32 cpu_id,UINT8 bsp_flags) {
    if (bsp_flags) {
        color_printk(GREEN, BLACK, "CPU Manufacturer: %s  Model: %s\n",cpu_info.manufacturer_name, cpu_info.model_name);
        color_printk(GREEN, BLACK, "CPU Cores: %d  FundamentalFrequency: %ldMhz  MaximumFrequency: %ldMhz  BusFrequency: %ldMhz  TSCFrequency: %ldhz\n",cpu_info.cores_number,cpu_info.fundamental_frequency,cpu_info.maximum_frequency,cpu_info.bus_frequency,cpu_info.tsc_frequency);

        __asm__ __volatile__ (
                "xorq       %%rdx,	%%rdx	    \n\t"
                "movq       $0xC4500,%%rax	    \n\t"    //bit8-10投递模式init101 ，bit14 1 ，bit18-19投递目标11所有处理器（不包括自身）
                "movq       $0x830,%%rcx	    \n\t"    //INIT IPI
                "wrmsr                      	\n\t"

                "movq       $0x5000,%%rcx	    \n\t"    //延时
                "loop_delay1:             	    \n\t"
                "loopq      loop_delay1	        \n\t"
                "movq       $0x830,%%rcx      	\n\t"
                "movq       $0xC4610,%%rax	    \n\t"   //Start-up IPI //bit0-7处理器启动地址000VV000的中间两位 ，bit8-10投递模式start-up110 ，bit14 1 ，bit18-19投递目标11所有处理器（不包括自身）
                "wrmsr	                        \n\t"

                "movq       $0x50000,%%rcx	    \n\t"   //延时
                "loop_delay2:                	\n\t"
                "loopq      loop_delay2         \n\t"
                "movq       $0x830,%%rcx    	\n\t"
                "wrmsr	                        \n\t"   //Start-up IPI
                :: :"%rax", "%rcx", "%rdx");
    }

    color_printk(GREEN, BLACK, "CPU%d init successful\n", cpu_id);

    init_cpu_num++;

    while(1){
        if(init_cpu_num == cpu_info.cores_number)
            break;
    }

    return;
}