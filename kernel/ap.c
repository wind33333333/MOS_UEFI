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
#include "syscall.h"

__attribute__((section(".init.data"))) UINT64 ap_rsp;
__attribute__((section(".init.data"))) UINT64 ap_boot_loader_address;

//多核处理器初始化
__attribute__((section(".init_text"))) void init_ap(void) {
    memcpy(_apboot_start, (void*)ap_boot_loader_address,_apboot_end-_apboot_start);                 //把ap核初始化代码复制到过去
    ap_rsp = (UINT64)LADDR_TO_HADDR(alloc_pages((cpu_info.logical_processors_number-1)*4));            //每个ap核分配16K栈
    map_pages((UINT64)HADDR_TO_LADDR(ap_rsp),ap_rsp,(cpu_info.logical_processors_number-1)*4,PAGE_ROOT_RW);

    UINT32 counter;
    //bit8-10投递模式init101 ，bit14 1 ，bit18-19投递目标11所有处理器（不包括自身）
    WRMSR(APIC_INTERRUPT_COMMAND_MSR,0xC4500);

    counter=0x5000;
    while (counter !=0 )  //延时
        counter--;

    //Start-up IPI bit0-7处理器启动实模式物理地址VV000的高两位 ，bit8-10投递模式start-up110 ，bit14 1 ，bit18-19投递目标11所有处理器（不包括自身）
    WRMSR(APIC_INTERRUPT_COMMAND_MSR,(ap_boot_loader_address>>12)&0xFF|0xC4600);

    counter=0x5000;
    while (counter !=0 )  //延时
        counter--;

    WRMSR(APIC_INTERRUPT_COMMAND_MSR,(ap_boot_loader_address>>12)&0xFF|0xC4600);      //Start-up IPI

    return;
}

__attribute__((section(".init_text"))) void ap_main(void){
    UINT32 apic_id,cpu_id,tmp;
    CPUID(tmp,tmp,tmp,apic_id,0xB,0x1);        //获取apic_ia
    cpu_id = apicid_to_cpuid(apic_id);
    init_cpu_amode();
    SET_CR3(HADDR_TO_LADDR(pml4t));
    LGDT(gdt_ptr,0x8UL,0x10UL);
    LTR(TSS_DESCRIPTOR_START_INDEX*8+cpu_id*16);
    LIDT(idt_ptr);
    init_apic();
    init_syscall();
    color_printk(GREEN, BLACK, "CPUID:%d APICID:%d init successful\n", cpu_id,apic_id);
    while(1);
}