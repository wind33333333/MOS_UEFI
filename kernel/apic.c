#include "apic.h"
#include "cpu.h"

INIT_TEXT void init_apic(void) {
    uint64 value;

    //region IA32_APIC_BASE_MSR (MSR 0x1B)
    //X2APIC（bit 10）：作用：如果该位被设置为 1，处理器启用 X2APIC 模式。
    //EN（bit 11）：作用：控制是否启用本地 APIC。设置为 1 时启用本地 APIC；设置为 0 时禁用。
    //BSP（bit 9）：作用：标记该处理器是否是系统的启动处理器（BSP）。系统启动时，BSP 是首先执行初始化代码的 CPU，其它处理器是 AP（Application Processors，应用处理器）。
    //APIC Base Address（bit 12-31）：作用：指定本地 APIC 的基地址。默认情况下，APIC 基地址为 0xFEE00000，但该值可以通过修改来改变，前提是该地址对齐到 4KB。
    //endregion
    value=rdmsr(IA32_APIC_BASE_MSR);
    value |= 0xC00;
    wrmsr(IA32_APIC_BASE_MSR,value);

    //SVR寄存器 bit0-7伪中断号，bit8启用local apic bit12禁用自动广播EOI
    value=rdmsr(APIC_SPURIOUS_VECTOR_MSR);
    value |= 0x1100;
    wrmsr(APIC_SPURIOUS_VECTOR_MSR,value);

    //TPR任务优先级寄存器
    wrmsr(APIC_TASK_PRIORITY_MSR,0x10);

    //热传感器LVT寄存器 bit0-7中断号，bit8-10投递模式000 fixed, bit16屏蔽标志 0未屏蔽 1屏蔽
    wrmsr(APIC_LVT_THERMAL_SENSOR_MSR,0x10022);

    //性能计数器LVT寄存器 bit0-7中断号，bit8-10投递模式000 fixed, bit16屏蔽标志 0未屏蔽 1屏蔽
    wrmsr(APIC_LVT_PERF_COUNTER_MSR,0x10023);

    //本地中断LINT0寄存器 bit0-7中断号，bit8-10投递模式000 fixed, bit13电平触发极性0高电平触发 1低电平触发,bit15触发模式0边沿 1电平，bit16屏蔽标志 0未屏蔽 1屏蔽
    wrmsr(APIC_LVT_LINT0_MSR,0x10024);

    //APIC_LVT_LINT1_MSR bit0-7中断号，bit8-10投递模式000 fixed, bit13电平触发极性0高电平触发 1低电平触发, bit15触发模式0边沿 1电平，bit16屏蔽标志 0未屏蔽 1屏蔽
    wrmsr(APIC_LVT_LINT1_MSR,0x10025);

    //错误LVT寄存器 bit0-7中断号，bit16屏蔽标志 0未屏蔽 1屏蔽
    wrmsr(APIC_LVT_ERROR_MSR,0x10026);
}

void enable_apic_time (uint64 time,uint32 model,uint32 ivt){

    uint32 model_ivt = model | ivt;
    //定时器LVT寄存器 bit0-7中断向量号,bit16屏蔽标志 0未屏蔽 1屏蔽,bit17 18 00/一次计数 01/周期计数 10/TSC-Deadline
    wrmsr(APIC_LVT_TIMER_MSR,model | ivt);

    if(model == APIC_TSC_DEADLINE){
        uint32 tmp;
        uint64 timestamp;
        rdtscp(&tmp,&timestamp);
        timestamp += time;
        wrmsr(IA32_TSC_DEADLINE,timestamp);
    } else {
        //分频配置寄存器 bit0 bit1 bit3 0:2 1:4 2:8 3:16 8:32 9:64 0xA:128 0xB:1
        wrmsr(APIC_DIVIDE_CONFIG_MSR, 0xA);

        //初始计数寄存器
        wrmsr(APIC_INITIAL_COUNT_MSR, time);
    }

}