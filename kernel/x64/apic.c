#include "apic.h"
#include "cpu.h"
#include "../include/printk.h"


INIT_TEXT void init_apic(void) {
    uint64 value;

    //region IA32_APIC_BASE_MSR (MSR 0x1B)
    //X2APIC（bit 10）：作用：如果该位被设置为 1，处理器启用 X2APIC 模式。
    //EN（bit 11）：作用：控制是否启用本地 APIC。设置为 1 时启用本地 APIC；设置为 0 时禁用。
    //BSP（bit 9）：作用：标记该处理器是否是系统的启动处理器（BSP）。系统启动时，BSP 是首先执行初始化代码的 CPU，其它处理器是 AP（Application Processors，应用处理器）。
    //APIC Base Address（bit 12-31）：作用：指定本地 APIC 的基地址。默认情况下，APIC 基地址为 0xFEE00000，但该值可以通过修改来改变，前提是该地址对齐到 4KB。
    //endregion
    value=asm_rdmsr(IA32_APIC_BASE_MSR);
    value |= 0xC00;
    asm_wrmsr(IA32_APIC_BASE_MSR,value);


    // ==========================================================
    // 1. 获取伪中断寄存器的当前值
    // ==========================================================
    value = asm_rdmsr(APIC_SPURIOUS_VECTOR_MSR);
    // 基础配置：开启 Local APIC (Bit 8) + 伪中断向量号设为 0xFF (Bit 0-7)
    // 此时 value 的掩码是 0x01FF
    value |= 0x01FF;

    // ==========================================================
    // 2. 动态探测是否支持“禁用 EOI 广播” (防 #GP 死机神技)
    // ==========================================================
    // 读取 APIC Version Register (在 x2APIC 下是 MSR 0x803)
    uint32 apic_version = asm_rdmsr(APIC_VERSION_MSR);

    // 检查 Bit 24 (Directed EOI Support)
    if (apic_version & (1 << 24)) {
        // 如果硬件支持，才敢把 Bit 12 置为 1！
        value |= 0x1000;
        color_printk(GREEN, BLACK, "[APIC] EOI-Broadcast Suppression ENABLED.\n");
    } else {
        color_printk(YELLOW, BLACK, "[APIC] EOI-Broadcast Suppression NOT supported (VirtualBox?), skipped.\n");
    }
    // ==========================================================
    // 3. 安全写入 SVR
    // ==========================================================
    asm_wrmsr(APIC_SPURIOUS_VECTOR_MSR, value);

    //TPR任务优先级寄存器
    asm_wrmsr(APIC_TASK_PRIORITY_MSR,0x0);

    //热传感器LVT寄存器 bit0-7中断号，bit8-10投递模式000 fixed, bit16屏蔽标志 0未屏蔽 1屏蔽
    asm_wrmsr(APIC_LVT_THERMAL_SENSOR_MSR,0x10022);

    //性能计数器LVT寄存器 bit0-7中断号，bit8-10投递模式000 fixed, bit16屏蔽标志 0未屏蔽 1屏蔽
    asm_wrmsr(APIC_LVT_PERF_COUNTER_MSR,0x10023);

    //本地中断LINT0寄存器 bit0-7中断号，bit8-10投递模式000 fixed, bit13电平触发极性0高电平触发 1低电平触发,bit15触发模式0边沿 1电平，bit16屏蔽标志 0未屏蔽 1屏蔽
    asm_wrmsr(APIC_LVT_LINT0_MSR,0x10024);

    //APIC_LVT_LINT1_MSR bit0-7中断号，bit8-10投递模式000 fixed, bit13电平触发极性0高电平触发 1低电平触发, bit15触发模式0边沿 1电平，bit16屏蔽标志 0未屏蔽 1屏蔽
    asm_wrmsr(APIC_LVT_LINT1_MSR,0x10025);

    //错误LVT寄存器 bit0-7中断号，bit16屏蔽标志 0未屏蔽 1屏蔽
    asm_wrmsr(APIC_LVT_ERROR_MSR,0x10026);
}

void enable_apic_time (uint64 time,uint32 model,uint32 ivt){

    uint32 model_ivt = model | ivt;
    //定时器LVT寄存器 bit0-7中断向量号,bit16屏蔽标志 0未屏蔽 1屏蔽,bit17 18 00/一次计数 01/周期计数 10/TSC-Deadline
    asm_wrmsr(APIC_LVT_TIMER_MSR,model_ivt);


    if(model == APIC_TSC_DEADLINE){
        uint64 cur_tsc= asm_rdtsc();
        uint64 timestamp=cur_tsc + time;
        asm_wrmsr(IA32_TSC_DEADLINE,timestamp);
    } else {
        //分频配置寄存器 bit0 bit1 bit3 0:2 1:4 2:8 3:16 8:32 9:64 0xA:128 0xB:1
        asm_wrmsr(APIC_DIVIDE_CONFIG_MSR, 0xB);
        //初始计数寄存器
        asm_wrmsr(APIC_INITIAL_COUNT_MSR, time);
    }

}