#include "idt.h"

//全局中断描述符表,放在直读段初始化进行页保护
RODATA __attribute__((aligned(4096))) const idt_t idt = {0};

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