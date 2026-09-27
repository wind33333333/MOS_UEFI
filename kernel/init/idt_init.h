#include "moslib.h"
#pragma once

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
    idt_t  *base;
}__attribute__((packed))idt_ptr_t;


// 假设你之前定义的 IDTR 结构体名叫 idtr_t
static inline void asm_lidt(const idt_ptr_t *idt_ptr) {
    __asm__ __volatile__(
        "lidt %0 \n\t"
        :
        /*
         * 【架构师修复】：必须加 * 解引用！
         * 这样 GCC 就会生成正确的内存寻址，例如: lidt (%rdi)
         * 而不是把你指针变量在栈上的地址喂给 CPU。
         */
        : "m"(*idt_ptr)
        : "memory"      // 内存屏障，非常正确！
    );
}

extern idt_t idt;

// 引入汇编里暴露的地址表 (极其优雅，免去了写 256 行 extern)
extern uint64 isr_stub_table[];

void tmp_idt_init(void);