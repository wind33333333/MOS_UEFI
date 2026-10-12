#include "moslib.h"
#include "printk.h"


int64 do_syscall_dispatcher(uint64 arg1, uint64 arg2, uint64 arg3, uint64 arg4, uint64 arg5) {
    // 从当前线程或暂存寄存器获取系统调用号 (这里从 r12 读取，或通过栈帧结构体传递)
    register uint64 sys_num asm("r12");

    switch (sys_num) {
        case 0: // sys_read
            return PR_INFO("syscall!!\n");
            break;
        // case 1: // sys_write / sys_print
        //     return sys_write((int)arg1, (const char *)arg2, (size_t)arg3);
        // case 60: // sys_exit
        //     sys_exit((int)arg1);
        //     return 0;
        default:
            return -38; // -ENOSYS (函数未实现)
    }
}