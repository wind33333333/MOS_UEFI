#include "apic.h"

__attribute__((section(".init_text"))) void init_apic(void) {
    __asm__ __volatile__ (
//region IA32_APIC_BASE (MSR 0x1B)
//X2APIC（bit 10）：作用：如果该位被设置为 1，处理器启用 X2APIC 模式。X2APIC 是 APIC 的扩展版本，提供了更多的功能，例如更大的中断目标地址空间。
//EN（bit 11）：作用：控制是否启用本地 APIC。设置为 1 时启用本地 APIC；设置为 0 时禁用。
//BSP（bit 9）：作用：标记该处理器是否是系统的启动处理器（BSP）。系统启动时，BSP 是首先执行初始化代码的 CPU，其它处理器是 AP（Application Processors，应用处理器）。
//APIC Base Address（bit 12-31）：作用：指定本地 APIC 的基地址。默认情况下，APIC 基地址为 0xFEE00000，但该值可以通过修改来改变，前提是该地址对齐到 4KB。
////endregion
            "movl     $0x1b,%%ecx  \n\t"         //IA32_APIC_BASE=0x1b 寄存器
            "rdmsr                 \n\t"
            "orl      $0xc00,%%eax \n\t"         //bit10启用x2apic ,bit11启用xapic
            "wrmsr                 \n\t"

            "movl   $0x80f,%%ecx   \n\t"         //SVR寄存器
            "rdmsr                 \n\t"
            "btsl    $8,%%eax      \n\t"         //bit0-7伪中断号，bit8启用local apic
            "btrl    $12,%%eax     \n\t"         //bit12禁用自动广播EOI
            "wrmsr                 \n\t"

            "movl   $0x808,%%ecx  \n\t"         //TPR优先级寄存器
            "xorl   %%edx,%%edx   \n\t"
            "movl   $0x10,%%eax   \n\t"
            "wrmsr                \n\t"

//            //qemu操作CMCI寄存器会报错暂时禁用
//            "movl $0x82F,%%ecx \n\t"           //CMCI寄存器
//            "movl $0x0,%%edx \n\t"
//            "movl $0x10021,%%eax \n\t"         //bit0-7中断号，bit8-10投递模式000 fixed, bit16屏蔽标志 0未屏蔽 1屏蔽
//            "wrmsr \n\t"
//            "1:\tjmp 1b \n\t"

            "movl   $0x833,%%ecx    \n\t"          //温度传感寄存器
            "xorl   %%edx,%%edx     \n\t"
            "movl   $0x10022,%%eax  \n\t"          //bit0-7中断号，bit8-10投递模式000 fixed, bit16屏蔽标志 0未屏蔽 1屏蔽
            "wrmsr                  \n\t"

            "movl   $0x834,%%ecx    \n\t"          //性能监控寄存器
            "xorl   %%edx,%%edx      \n\t"
            "movl   $0x10023,%%eax  \n\t"          //bit0-7中断号，bit8-10投递模式000 fixed, bit16屏蔽标志 0未屏蔽 1屏蔽
            "wrmsr                  \n\t"

            "movl   $0x835,%%ecx    \n\t"          //LINT0寄存器
            "xorl   %%edx,%%edx     \n\t"
            "movl   $0x10024,%%eax  \n\t"          //bit0-7中断号，bit8-10投递模式000 fixed, bit13电平触发极性0高电平触发 1低电平触发,bit15触发模式0边沿 1电平，bit16屏蔽标志 0未屏蔽 1屏蔽
            "wrmsr                  \n\t"

            "movl   $0x836,%%ecx    \n\t"          //LINT1寄存器
            "xorl   %%edx,%%edx     \n\t"
            "movl   $0x10025,%%eax  \n\t"          //bit0-7中断号，bit8-10投递模式000 fixed, bit13电平触发极性0高电平触发 1低电平触发, bit15触发模式0边沿 1电平，bit16屏蔽标志 0未屏蔽 1屏蔽
            "wrmsr                  \n\t"

            "movl   $0x837,%%ecx    \n\t"          //错误寄存器
            "xorl   %%edx,%%edx     \n\t"
            "movl   $0x10026,%%eax  \n\t"          //bit0-7中断号，bit16屏蔽标志 0未屏蔽 1屏蔽
            "wrmsr                  \n\t"
            :: :"%rax", "%rcx", "%rdx");

    return;
}

void enable_apic_time (UINT64 time,UINT32 model,UINT32 ivt){

    UINT32 model_ivt = model | ivt;
    __asm__ __volatile(   \
         "xorl   %%edx,%%edx     \n\t"
         "movl   %0,%%eax        \n\t"         /*bit0-7中断向量号,bit16屏蔽标志 0未屏蔽 1屏蔽,bit17 18 00/一次计数 01/周期计数 10/TSC-Deadline*/
         "movl   $0x832,%%ecx    \n\t"         /*定时器模式配置寄存器*/
         "wrmsr                  \n\t"
         ::"m"(model_ivt):"%rcx","%rdx");

    if(model == APIC_TSC_DEADLINE){
        __asm__ __volatile__(
         "rdtscp                    \n\t"
         "shl $32,%%rdx             \n\t"
         "or %%rdx,%%rax            \n\t"
         "add %0,%%rax              \n\t"
         "mov %%rax,%%rdx           \n\t"
         "mov $0xFFFFFFFF,%%rcx     \n\t"
         "and %%rcx,%%rax           \n\t"
         "shr $32,%%rdx             \n\t"
         "mov $0x6E0,%%ecx          \n\t"   /*IA32_TSC_DEADLINE寄存器 TSC-Deadline定时模式 */
         "wrmsr                     \n\t"
          ::"m"(time):"%rax","%rcx","%rdx");

    } else{
        __asm__ __volatile__(
         "xorl   %%edx,%%edx     \n\t"
         "movl   $0xA,%%eax      \n\t"        /* bit0 bit1 bit3 0:2 1:4 2:8 3:16 8:32 9:64 0xA:128 0xB:1*/
         "movl   $0x83E,%%ecx    \n\t"        /*分频器寄存器*/
         "wrmsr                  \n\t"

         "mov   %0,%%eax         \n\t"
         "xor   %%rdx ,%%rdx     \n\t"
         "mov   $0x838,%%ecx     \n\t"        /*定时器计数器寄存器*/
         "wrmsr                  \n\t"
         ::"m"(time):"%rax","%rcx","%rdx");
    }

    return;
}