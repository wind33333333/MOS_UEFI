#include "moslib.h"
#include "printk.h"
#include "../x64/cpu_registers.h"
#include "syscall_entry.h"


int64 sys_print(cpu_registers_t *regs) {
    PR_INFO("syscall !!!\n");
}


// 1. 定义统一的系统调用函数指针类型
typedef int64 (*syscall_f)(cpu_registers_t *regs);

// 2. 静态系统调用表 (大小覆盖支持的系统调用号上限)

syscall_f syscall_table[MAX_SYSCALL_NUM] = {
    [0]  = sys_print,

};
