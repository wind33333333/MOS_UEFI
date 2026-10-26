#include "idt.h"

//全局中断描述符表,放在直读段初始化进行页保护
RODATA __attribute__((aligned(4096))) const idt_t idt = {0};