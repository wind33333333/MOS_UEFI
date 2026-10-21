#include "moslib.h"
#include "printk.h"
#include "../x64/cpu_registers.h"
#include "syscall_entry.h"
#include "../time/timer.h"

// ==========================================================
// x86_64 标准系统调用号定义 (严格对齐 Linux ABI)
// ==========================================================

/* ---------------- 文件与 I/O 基础操作 ---------------- */
#define __NR_read           0   /* 从文件描述符读取数据 */
#define __NR_write          1   /* 向文件描述符写入数据 */
#define __NR_open           2   /* 打开文件或设备 */
#define __NR_close          3   /* 关闭文件描述符 */
#define __NR_stat           4   /* 获取文件状态/属性 */
#define __NR_fstat          5   /* 通过文件描述符获取文件状态 */
#define __NR_lseek          8   /* 移动文件读写指针 (Seek) */
#define __NR_ioctl          16  /* 设备底层控制接口 (向驱动发送特定控制码) */

/* ---------------- 内存管理操作 ---------------- */
#define __NR_mmap           9   /* 映射虚拟内存 (现代 malloc 底层核心机制) */
#define __NR_mprotect       10  /* 修改内存页的保护属性 (R/W/X/NX) */
#define __NR_munmap         11  /* 解除虚拟内存映射 */
#define __NR_brk            12  /* 调整数据段末尾指针 (传统堆内存分配) */

/* ---------------- 进程与线程管理 ---------------- */
#define __NR_sched_yield    24  /* 主动让出 CPU 时间片给其他就绪任务 */
#define __NR_nanosleep      35  /* 高精度休眠 (代替你的 usleep) */
#define __NR_getpid         39  /* 获取当前进程 ID (PID) */
#define __NR_clone          56  /* 创建轻量级进程/线程 (现代 fork 的底层实现) */
#define __NR_fork           57  /* 创建子进程 (传统机制) */
#define __NR_execve         59  /* 加载并执行新的 ELF 可执行文件 */
#define __NR_exit           60  /* 终止当前进程并清理资源 */
#define __NR_wait4          61  /* 等待子进程退出并回收资源 (防止僵尸进程) */
#define __NR_kill           62  /* 向目标进程发送信号 */
#define __NR_gettimeofday   96  /* 获取系统当前高精度时间 */

/* ---------------- 网络与 IPC通信 ---------------- */
#define __NR_socket         41  /* 创建套接字 */
#define __NR_connect        42  /* 发起网络连接 */
#define __NR_accept         43  /* 接收网络连接 */
#define __NR_sendto         44  /* 发送网络数据包 */
#define __NR_recvfrom       45  /* 接收网络数据包 */

// ==========================================================
// MOS_UEFI 自研扩展系统调用 (建议从 500 以后开始，避开标准号)
// ==========================================================
#define __NR_color_printk   500 /* 你的内核控制台彩色打印专用调用 */
#define __NR_get_fb_info    501 /* 获取 FrameBuffer 屏幕参数信息 */

// 1. 定义统一的系统调用函数指针类型
typedef int64 (*syscall_f)(cpu_registers_t *regs);


int64 sys_print(cpu_registers_t *regs);

// 2. 静态系统调用表 (大小覆盖支持的系统调用号上限)
syscall_f syscall_table[MAX_SYSCALL_NUM] = {
    [0]  = sys_print,
    [__NR_nanosleep] = sys_ksleep_us,
    [__NR_color_printk] = sys_color_printk,
};


static inline long syscall0(long number) {
    long ret;
    __asm__ volatile ("syscall"
                      : "=a" (ret)
                      : "a" (number)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall1(long number, unsigned long arg1) {
    long ret;
    __asm__ volatile ("syscall"
                      : "=a" (ret)
                      : "a" (number), "D" (arg1)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall2(long number, unsigned long arg1, unsigned long arg2) {
    long ret;
    __asm__ volatile ("syscall"
                      : "=a" (ret)
                      : "a" (number), "D" (arg1), "S" (arg2)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long number, unsigned long arg1, unsigned long arg2, unsigned long arg3) {
    long ret;
    __asm__ volatile ("syscall"
                      : "=a" (ret)
                      : "a" (number), "D" (arg1), "S" (arg2), "d" (arg3)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall4(long number, unsigned long arg1, unsigned long arg2, unsigned long arg3, unsigned long arg4) {
    long ret;
    // 强制把 arg4 绑定到 r10 寄存器
    register long _arg4 __asm__("r10") = arg4;
    __asm__ volatile ("syscall"
                      : "=a" (ret)
                      // 用 "r" 让编译器直接使用已绑定好 r10 的 _arg4
                      : "a" (number), "D" (arg1), "S" (arg2), "d" (arg3), "r" (_arg4)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall5(long number, unsigned long arg1, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5) {
    long ret;
    register long _arg4 __asm__("r10") = arg4;
    register long _arg5 __asm__("r8")  = arg5;
    __asm__ volatile ("syscall"
                      : "=a" (ret)
                      : "a" (number), "D" (arg1), "S" (arg2), "d" (arg3), "r" (_arg4), "r" (_arg5)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall6(long number, unsigned long arg1, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5, unsigned long arg6) {
    long ret;
    register long _arg4 __asm__("r10") = arg4;
    register long _arg5 __asm__("r8")  = arg5;
    register long _arg6 __asm__("r9")  = arg6;
    __asm__ volatile ("syscall"
                      : "=a" (ret)
                      : "a" (number), "D" (arg1), "S" (arg2), "d" (arg3), "r" (_arg4), "r" (_arg5), "r" (_arg6)
                      : "rcx", "r11", "memory");
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
int32 color_printf(uint32 FRcolor, uint32 BKcolor, const char *fmt, ...) {
    // 1. 在用户态栈上分配临时缓冲区
    char user_buf[512];
    va_list args;

    // 2. 使用标准库或用户态轻量级 vsnprintf 在本地完成可变参和格式化拼接
    va_start(args, fmt);
    // 注意：在标准 libc 中是 vsnprintf，如果你的用户态是自研 Runtime，调用对应的实现即可
    int32 len = vsnprintf(user_buf, sizeof(user_buf), fmt, args);
    va_end(args);

    if (len < 0) {
        return -1;
    }

    // 如果内容超长被截断，确保末尾有结束符
    if (len >= (int32)sizeof(user_buf)) {
        user_buf[sizeof(user_buf) - 1] = '\0';
    }

    // 3. 通过 syscall 指令安全地将参数传递给内核的 sys_color_printk
    // 寄存器映射：
    // rax = __NR_color_printk (42)
    // rdi = FRcolor
    // rsi = BKcolor
    // rdx = user_buf (用户空间字符串指针)
    long ret = syscall3(42, (unsigned long)FRcolor, (unsigned long)BKcolor, (unsigned long)user_buf);

    return (int)ret;
}


int32 sleep_us(uint64 delay_us) {
    return (int32)syscall1(__NR_nanosleep,delay_us);
}