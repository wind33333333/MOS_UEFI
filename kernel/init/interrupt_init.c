#include "../x64/interrupt_dispatcher.h"
#include "printk.h"


static inline void exception_init() {
    register_isr(0,do_divide_error,NULL,"Divide Error");
    register_isr(1,do_debug,NULL,"Debug Exception");
    //2号 NMI: 不可屏蔽中断 (Non-Maskable Interrupt)，单独的中断入口
    register_isr(3,do_int3,NULL,"Breakpoint/INT3");
    register_isr(4,do_overflow,NULL,"Overflow/INTO");
    register_isr(5,do_bounds,NULL,"BOUND Range Exceeded");
    register_isr(6,do_undefined_opcode,NULL,"Undefined Opcode");
    register_isr(7,do_dev_not_available,NULL,"Device Not Available");
    register_isr(8,do_double_fault,NULL,"Double Fault");
    register_isr(9,do_coprocessor_segment_overrun,NULL,"Coprocessor Segment Overrun");
    register_isr(10,do_invalid_tss,NULL,"Invalid TSS");
    register_isr(11,do_segment_not_present,NULL,"Segment Not Present");
    register_isr(12,do_stack_segment_fault,NULL,"Stack Segment Fault");
    register_isr(13,do_general_protection,NULL,"General Protection");
    register_isr(14,do_page_fault,NULL,"Page Fault");
    //15保留
    register_isr(16,do_x87_FPU_error,NULL,"x87 FPU Floating-Point Error");
    register_isr(17,do_alignment_check,NULL,"Alignment Check");
    register_isr(18,do_machine_check,NULL,"Machine Check");
    register_isr(19,do_SIMD_exception,NULL,"SIMD Exception");
    register_isr(20,do_virtualization_exception,NULL,"Virtualization Exception");
    // [21] - [31] 为 Intel 保留异常或较新的硬件异常 (如 21号 #CP 控制保护异常)，默认初始化为 NULL
}

// 引用汇编中连续中断桩的基地址
extern const char interrupt_entry_base[];

/**
 * @brief 内部函数：组装 16 字节的 IDT 描述符
 */

static inline void idt_set_descriptor(uint8 vector , uint8 attributes, uint8 ist) {
    uint64 isr_addr = (uint64)interrupt_entry_base + ((uint64)vector << 4);
    idt_desc_t *desc = (idt_desc_t*)(&idt.desc[vector]);

    desc->offset_low       = isr_addr & 0xFFFF;
    desc->segment_selector = 0x08; // 你的内核代码段选择子 (根据你的 GDT 调整)
    desc->ist              = ist & 0x07;
    desc->attributes       = attributes;
    desc->offset_mid       = (isr_addr >> 16) & 0xFFFF;
    desc->offset_high      = (isr_addr >> 32) & 0xFFFFFFFF;
    desc->reserved         = 0; // 必须为 0
}


/**
 * @brief 初始化临时中断系统 (在内核早期初始化时调用)
 */
void idt_init(void) {
    // 循环挂载 256 个中断门
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_descriptor(i, IDT_INTERRUPT_DESC,0);
    }

    // 特例：如果是供用户态引发的异常/系统调用，可以改属性
    idt_set_descriptor(3, IDT_USER_DESC,0);

    // =========================================================
    // 👑 架构师核心修改：为致命异常颁发 IST 免死金牌！
    // =========================================================
    idt_set_descriptor(8, IDT_INTERRUPT_DESC,1);// 8: Double Fault (#DF) 双重故障
    idt_set_descriptor(2, IDT_INTERRUPT_DESC,2);// 2: NMI 不可屏蔽中断
    idt_set_descriptor(18, IDT_INTERRUPT_DESC,3);// 18: Machine Check (#MC) 机器检查
    idt_set_descriptor(1, IDT_INTERRUPT_DESC,4);// 1: Debug (#DB) 调试异常

    // 装载 IDTR
    idt_ptr_t idtr;
    idtr.limit = sizeof(idt) - 1;
    idtr.base = &idt;
    asm_lidt(&idtr);

    exception_init();   //配置前32个异常

    // 打开 CPU 全局中断标志
    asm_sti();

    PR_OK("IDT Initialized with 256 vectors.\n");
}
