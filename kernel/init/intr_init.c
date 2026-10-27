#include "../x64/ecx_dispatcher.h"
#include "../x64/idt.h"
#include "printk.h"

// 引用汇编中连续中断桩的基地址
extern const char irq_ecx_entry[];

/**
 * @brief 内部函数：组装 16 字节的 IDT 描述符
 */

static inline void idt_set_descriptor(uint8 vector , uint8 attributes, uint8 ist) {
    uint64 isr_addr = (uint64)irq_ecx_entry + ((uint64)vector << 4);
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
    idt_set_descriptor(X86_EXC_BP, IDT_USER_DESC,0);

    // =========================================================
    // 👑 架构师核心修改：为致命异常颁发 IST 免死金牌！
    // =========================================================
    idt_set_descriptor(X86_EXC_DF, IDT_INTERRUPT_DESC,1);// 8: Double Fault (#DF) 双重故障
    idt_set_descriptor(X86_EXC_NMI, IDT_INTERRUPT_DESC,2);// 2: NMI 不可屏蔽中断
    idt_set_descriptor(X86_EXC_MC, IDT_INTERRUPT_DESC,3);// 18: Machine Check (#MC) 机器检查
    idt_set_descriptor(X86_EXC_DB, IDT_INTERRUPT_DESC,4);// 1: Debug (#DB) 调试异常

    // 装载 IDTR
    idt_ptr_t idtr;
    idtr.limit = sizeof(idt) - 1;
    idtr.base = &idt;
    asm_lidt(&idtr);

    // 打开 CPU 全局中断标志
    asm_sti();

    PR_OK("IDT Initialized with 256 vectors.\n");
}

void tmp_exception_init() {
    register_exception(X86_EXC_DE,  do_divide_error);
    register_exception(X86_EXC_DB,  do_debug);
    // X86_EXC_NMI (2号) 通常需要极其特殊的硬件级处理机制，所以不在这里用普通异常接口注册
    register_exception(X86_EXC_BP,  do_int3);
    register_exception(X86_EXC_OF,  do_overflow);
    register_exception(X86_EXC_BR,  do_bounds);
    register_exception(X86_EXC_UD,  do_undefined_opcode);
    register_exception(X86_EXC_NM,  do_dev_not_available);
    register_exception(X86_EXC_DF,  do_double_fault);
    register_exception(X86_EXC_CSO, do_coprocessor_segment_overrun);
    register_exception(X86_EXC_TS,  do_invalid_tss);
    register_exception(X86_EXC_NP,  do_segment_not_present);
    register_exception(X86_EXC_SS,  do_stack_segment_fault);
    register_exception(X86_EXC_GP,  do_general_protection);
    register_exception(X86_EXC_PF,  do_page_fault);
    // 15 保留
    register_exception(X86_EXC_MF,  do_x87_FPU_error);
    register_exception(X86_EXC_AC,  do_alignment_check);
    register_exception(X86_EXC_MC,  do_machine_check);
    register_exception(X86_EXC_XM,  do_SIMD_exception);
    register_exception(X86_EXC_VE,  do_virtualization_exception);
    // [21] - [31] 为 Intel 保留异常或较新的硬件异常 (如 X86_EXC_CP 控制保护异常)，默认初始化为 NULL
    PR_INFO("0-31 Temporary Exception Init Success!\n");
}