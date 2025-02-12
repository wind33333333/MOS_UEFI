#include "ap.h"
#include "printk.h"
#include "cpu.h"
#include "vmm.h"
#include "acpi.h"
#include "gdt.h"
#include "idt.h"
#include "apic.h"
#include "tss.h"
#include "kpage_table.h"
#include "syscall.h"

INIT_DATA UINT64 ap_boot_loader_address;

//多核处理器初始化
INIT_TEXT void init_ap(void) {
    ap_main_ptr = &ap_main;
    ap_tmp_pml4t_ptr = (UINT64*)VA_TO_PA(&tmp_pml4t);
    apic_id_table_ptr = apic_id_table;
    ap_rsp_ptr = (UINT64)PA_TO_VA(bitmap_alloc_pages((cpu_info.logical_processors_number-1)*4));            //每个ap核分配16K栈
    bitmap_map_pages(VA_TO_PA(ap_rsp_ptr),(void*)ap_rsp_ptr,(cpu_info.logical_processors_number-1)*4,PAGE_ROOT_RW);
    memcpy(_apboot_start, (void*)ap_boot_loader_address,_apboot_end-_apboot_start);                 //把ap核初始化代码复制到过去

    UINT32 counter;
    //bit8-10投递模式init101 ，bit14 1 ，bit18-19投递目标11所有处理器（不包括自身）
    wrmsr(APIC_INTERRUPT_COMMAND_MSR,0xC4500);

    counter=0x5000;
    while (counter !=0 )  //延时
        counter--;

    //Start-up IPI bit0-7处理器启动实模式物理地址VV000的高两位 ，bit8-10投递模式start-up110 ，bit14 1 ，bit18-19投递目标11所有处理器（不包括自身）
    wrmsr(APIC_INTERRUPT_COMMAND_MSR,(ap_boot_loader_address>>12)&0xFF|0xC4600);

    counter=0x5000;
    while (counter !=0 )  //延时
        counter--;

    wrmsr(APIC_INTERRUPT_COMMAND_MSR,(ap_boot_loader_address>>12)&0xFF|0xC4600);      //Start-up IPI

    return;
}

INIT_TEXT void ap_main(void){
    UINT32 apic_id,cpu_id,tmp;
    cpuid(0xB,0x1,&tmp,&tmp,&tmp,&apic_id);        //获取apic_ia
    cpu_id = apicid_to_cpuid(apic_id);
    init_cpu_amode();
    set_cr3(kplm4t_ptr);
    lgdt(&gdt_ptr,0x8,0x10);
    ltr(TSS_DESCRIPTOR_START_INDEX*16+cpu_id*16);
    lidt(&idt_ptr);
    init_apic();
    init_syscall();
    color_printk(GREEN, BLACK, "CPUID:%d APICID:%d init successful\n", cpu_id,apic_id);
    while(1);
}