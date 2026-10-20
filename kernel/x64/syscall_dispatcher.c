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
    [42] = sys_color_printk,
};


static inline long syscall(long number, unsigned long arg1, unsigned long arg2, unsigned long arg3) {
    long ret;
    // 使用 x86_64 的 syscall 指令：
    // rax = 系统调用号, rdi = arg1, rsi = arg2, rdx = arg3
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (number), "D" (arg1), "S" (arg2), "d" (arg3)
        : "rcx", "r11", "memory" // syscall 指令会破坏 rcx 和 r11，memory 防止编译器乱序优化
    );
    return ret;
}


/**
 * @brief 用户态彩色可变参打印函数 (Ring 3 封装接口)
 * @param FRcolor 前景色
 * @param BKcolor 背景色
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @return int 实际打印的字符数，出错返回 -1
 */
int color_printf(uint32 FRcolor, uint32 BKcolor, const char *fmt, ...) {
    // 1. 在用户态栈上分配临时缓冲区
    char user_buf[512];
    va_list args;

    // 2. 使用标准库或用户态轻量级 vsnprintf 在本地完成可变参和格式化拼接
    va_start(args, fmt);
    // 注意：在标准 libc 中是 vsnprintf，如果你的用户态是自研 Runtime，调用对应的实现即可
    int len = vsnprintf(user_buf, sizeof(user_buf), fmt, args);
    va_end(args);

    if (len < 0) {
        return -1;
    }

    // 如果内容超长被截断，确保末尾有结束符
    if (len >= sizeof(user_buf)) {
        user_buf[sizeof(user_buf) - 1] = '\0';
    }

    // 3. 通过 syscall 指令安全地将参数传递给内核的 sys_color_printk
    // 寄存器映射：
    // rax = __NR_color_printk (42)
    // rdi = FRcolor
    // rsi = BKcolor
    // rdx = user_buf (用户空间字符串指针)
    long ret = syscall(42, (unsigned long)FRcolor, (unsigned long)BKcolor, (unsigned long)user_buf);

    return (int)ret;
}