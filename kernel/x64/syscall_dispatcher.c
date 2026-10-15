#include "moslib.h"
#include "printk.h"
#include "../x64/cpu_registers.h"


int64 sys_print(cpu_registers_t *regs) {
    PR_INFO("syscall !!!");
}


// 1. 定义统一的系统调用函数指针类型
typedef int64 (*syscall_f)(cpu_registers_t *regs);

// 2. 静态系统调用表 (大小覆盖支持的系统调用号上限)
#define MAX_SYSCALL_NUM 256
static syscall_f sys_call_table[MAX_SYSCALL_NUM] = {
    [0]  = sys_print,

};

// 3. 极速分发
int64 do_syscall_dispatcher(cpu_registers_t *regs) {
    uint64 sys_num = regs->rax; // 标准 x86_64 约定使用 RAX

    if (sys_num >= MAX_SYSCALL_NUM || sys_call_table[sys_num] == NULL) {
        return -38; // -ENOSYS
    }

    // 从栈帧结构体直接传参，安全且不破坏编译器寄存器分配
    return sys_call_table[sys_num](regs);
}