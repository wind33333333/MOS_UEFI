#include "ap.h"
#include "printk.h"
#include "cpu.h"
#include "memory.h"
#include "acpi.h"
#include "gdt.h"
#include "idt.h"
#include "apic.h"
#include "tss.h"

__attribute__((section(".init.data"))) UINT64 ap_rsp;

//多核处理器初始化
__attribute__((section(".init_text"))) void init_ap(void) {
    color_printk(GREEN, BLACK, "CPU Manufacturer: %s  Model: %s\n",cpu_info.manufacturer_name, cpu_info.model_name);
    color_printk(GREEN, BLACK, "CPU Cores: %d  FundamentalFrequency: %ldMhz  MaximumFrequency: %ldMhz  BusFrequency: %ldMhz  TSCFrequency: %ldhz\n",cpu_info.logical_processors_number,cpu_info.fundamental_frequency,cpu_info.maximum_frequency,cpu_info.bus_frequency,cpu_info.tsc_frequency);

    memcpy(&_apboot_start, LADDR_TO_HADDR(APBOOT_ADDR),&_apboot_end-&_apboot_start);                 //把ap核初始化代码复制到过去
    ap_rsp = (UINT64)LADDR_TO_HADDR(alloc_pages((cpu_info.logical_processors_number-1)*4));            //每个ap核分配16K栈
    map_pages((UINT64)HADDR_TO_LADDR(ap_rsp),ap_rsp,(cpu_info.logical_processors_number-1)*4,PAGE_ROOT_RW);

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

    return;
}

__attribute__((section(".init_text"))) void ap_main(void){
    UINT32 apic_id,cpu_id;
    GET_APICID(apic_id);
    cpu_id = apicid_to_cpuid(apic_id);
    init_cpu_mode();
    LGDT(gdt_ptr,0x8UL,0x10UL);
    LTR(TSS_DESCRIPTOR_START_INDEX*8+cpu_id*16);
    LIDT(idt_ptr);
    init_apic();
    SET_CR3(HADDR_TO_LADDR(pml4t));
    color_printk(GREEN, BLACK, "CPUID:%d APICID:%d init successful\n", cpu_id,apic_id);
    while(1);
}