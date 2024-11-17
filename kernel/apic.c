#include "apic.h"
#include "cpu.h"

__attribute__((section(".init_text"))) void init_apic(void) {
    UINT64 value;

    //region IA32_APIC_BASE_MSR (MSR 0x1B)
    //X2APIC（bit 10）：作用：如果该位被设置为 1，处理器启用 X2APIC 模式。
    //EN（bit 11）：作用：控制是否启用本地 APIC。设置为 1 时启用本地 APIC；设置为 0 时禁用。
    //BSP（bit 9）：作用：标记该处理器是否是系统的启动处理器（BSP）。系统启动时，BSP 是首先执行初始化代码的 CPU，其它处理器是 AP（Application Processors，应用处理器）。
    //APIC Base Address（bit 12-31）：作用：指定本地 APIC 的基地址。默认情况下，APIC 基地址为 0xFEE00000，但该值可以通过修改来改变，前提是该地址对齐到 4KB。
    //endregion
    RDMSR(IA32_APIC_BASE_MSR,value);
    value |= 0xC00;
    WRMSR(IA32_APIC_BASE_MSR,value);

    //SVR寄存器 bit0-7伪中断号，bit8启用local apic bit12禁用自动广播EOI
    RDMSR(APIC_SPURIOUS_VECTOR_MSR,value);
    value |= 0x1100;
    WRMSR(APIC_SPURIOUS_VECTOR_MSR,value);

    //TPR任务优先级寄存器
    WRMSR(APIC_TASK_PRIORITY_MSR,0x10);

    //热传感器LVT寄存器 bit0-7中断号，bit8-10投递模式000 fixed, bit16屏蔽标志 0未屏蔽 1屏蔽
    WRMSR(APIC_LVT_THERMAL_SENSOR_MSR,0x10022);

    //性能计数器LVT寄存器 bit0-7中断号，bit8-10投递模式000 fixed, bit16屏蔽标志 0未屏蔽 1屏蔽
    WRMSR(APIC_LVT_PERF_COUNTER_MSR,0x10023);

    //本地中断LINT0寄存器 bit0-7中断号，bit8-10投递模式000 fixed, bit13电平触发极性0高电平触发 1低电平触发,bit15触发模式0边沿 1电平，bit16屏蔽标志 0未屏蔽 1屏蔽
    WRMSR(APIC_LVT_LINT0_MSR,0x10024);

    //APIC_LVT_LINT1_MSR bit0-7中断号，bit8-10投递模式000 fixed, bit13电平触发极性0高电平触发 1低电平触发, bit15触发模式0边沿 1电平，bit16屏蔽标志 0未屏蔽 1屏蔽
    WRMSR(APIC_LVT_LINT1_MSR,0x10025);

    //错误LVT寄存器 bit0-7中断号，bit16屏蔽标志 0未屏蔽 1屏蔽
    WRMSR(APIC_LVT_ERROR_MSR,0x10026);

    return;
}

void enable_apic_time (UINT64 time,UINT32 model,UINT32 ivt){

    UINT32 model_ivt = model | ivt;
    //定时器LVT寄存器 bit0-7中断向量号,bit16屏蔽标志 0未屏蔽 1屏蔽,bit17 18 00/一次计数 01/周期计数 10/TSC-Deadline
    WRMSR(APIC_LVT_TIMER_MSR,model | ivt);

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