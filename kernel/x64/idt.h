#pragma once
#include "moslib.h"

// 门类型宏定义
#define IDT_INTERRUPT_DESC 0x8E // P=1, DPL=0, Type=E (硬件中断，自动关 IF)
#define IDT_TRAP_DESC      0x8F // P=1, DPL=0, Type=F (异常陷阱，不关 IF)
#define IDT_USER_DESC      0xEE // P=1, DPL=3, Type=E (系统调用等，允许 Ring3 触发)

// 1. x64 中断门描述符 (16 字节，严格对齐)
typedef struct {
    uint16 offset_low;
    uint16 segment_selector;
    uint8  ist;
    uint8  attributes;
    uint16 offset_mid;
    uint32 offset_high;
    uint32 reserved;
}__attribute__((packed)) idt_desc_t;

#define IDT_ENTRIES 256
typedef struct {
    idt_desc_t desc[IDT_ENTRIES];
}__attribute__((packed))idt_t;

// IDTR 寄存器结构
typedef struct idt_ptr_t{
    uint16 limit;
    const idt_t  *base;
}__attribute__((packed))idt_ptr_t;

void asm_lidt();