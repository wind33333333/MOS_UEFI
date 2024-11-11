#include "ap.h"
#include "printk.h"
#include "cpu.h"
#include "memory.h"
#include "acpi.h"
#include "gdt.h"
#include "idt.h"
#include "apic.h"
#include "tss.h"
#include "page.h"

__attribute__((section(".init.data"))) UINT64 ap_rsp;
__attribute__((section(".init.data"))) UINT64 ap_boot_loader_address;

//多核处理器初始化
__attribute__((section(".init_text"))) void init_ap(void) {
    memcpy(_apboot_start, (void*)ap_boot_loader_address,_apboot_end-_apboot_start);                 //把ap核初始化代码复制到过去
    ap_rsp = (UINT64)LADDR_TO_HADDR(alloc_pages((cpu_info.logical_processors_number-1)*4));            //每个ap核分配16K栈
    map_pages(HADDR_TO_LADDR(ap_rsp),ap_rsp,(cpu_info.logical_processors_number-1)*4,PAGE_ROOT_RW);
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
            "movq       $0xC4600,%%rax	    \n\t"   //Start-up IPI //bit0-7处理器启动实模式物理地址VV000的高两位 ，bit8-10投递模式start-up110 ，bit14 1 ，bit18-19投递目标11所有处理器（不包括自身）
            "orq        %%rbx,%%rax         \n\t"   //rbx保存传入的apboot地址高8位
            "wrmsr	                        \n\t"

            "movq       $0x50000,%%rcx	    \n\t"   //延时
            "loop_delay2:                	\n\t"
            "loopq      loop_delay2         \n\t"
            "movq       $0x830,%%rcx    	\n\t"
            "wrmsr	                        \n\t"   //Start-up IPI
            ::"b"((ap_boot_loader_address>>12)&0xFF):"%rax", "%rcx", "%rdx","memory");
    return;
}

__attribute__((section(".init_text"))) void ap_main(void){
    UINT32 apic_id,cpu_id;
    GET_APICID(apic_id);
    cpu_id = apicid_to_cpuid(apic_id);
    init_cpu_amode();
    LGDT(gdt_ptr,0x8UL,0x10UL);
    LTR(TSS_DESCRIPTOR_START_INDEX*8+cpu_id*16);
    LIDT(idt_ptr);
    init_apic();
    SET_CR3(HADDR_TO_LADDR(pml4t));
    color_printk(GREEN, BLACK, "CPUID:%d APICID:%d init successful\n", cpu_id,apic_id);
    while(1);
}