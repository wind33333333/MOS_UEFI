#include "idt.h"

//全局中断描述符表,放在直读段初始化进行页保护
RODATA __attribute__((aligned(4096))) const idt_t idt = {0};


// 引用汇编中连续中断桩的基地址
extern const char irq_ecx_entry[];

/**
 * @brief 内部函数：组装 16 字节的 IDT 描述符
 */

void idt_set_descriptor(uint8 vector , uint8 attributes, uint8 ist) {
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


void asm_lidt() {
    idt_ptr_t idt_ptr;
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = &idt;

    __asm__ __volatile__(
        "lidt %0 \n\t"
        :
        : "m"(idt_ptr)
        : "memory"      // 内存屏障，非常正确！
    );
}