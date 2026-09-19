#pragma once

#include "moslib.h"
#include "../init/idt_init.h"

//全局中断描述符表
__attribute__((aligned(4096))) idt_t idt;
