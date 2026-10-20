#include "printk.h"
#include "../init/uefi_init.h"

/* 格式化标志位定义 */
#define ZEROPAD    1        /* 用零填充 */
#define SIGN       2        /* 有符号/无符号长整型 */
#define PLUS       4        /* 显示正号 */
#define SPACE      8        /* 正号前留空格 */
#define LEFT       16       /* 左对齐 */
#define SPECIAL    32       /* 0x 前缀 */
#define SMALL      64       /* 使用小写十六进制字母 */

#define is_digit(c) ((c) >= '0' && (c) <= '9')

position_t Pos;

/* 内联汇编数学除法，用于数字格式化基数转换 */
#define do_div(n,base) ({ \
int32 __res; \
__asm__("divq %%rcx":"=a" (n),"=d" (__res):"0" (n),"1" (0),"c" (base)); \
__res; })

/**
 * @brief 在显存帧缓冲区指定坐标绘制单个 ASCII 字符
 */
void putchar(uint32 *fb, int32 Xsize, int32 x, int32 y, uint32 FRcolor, uint32 BKcolor,
             unsigned char font) {
    int32 i = 0, j = 0;
    uint32 *addr = NULL;
    unsigned char *fontp = NULL;
    int32 testval = 0;
    fontp = font_ascii[font];

    for (i = 0; i < 16; i++) {
        addr = fb + Xsize * (y + i) + x;
        testval = 0x100;
        for (j = 0; j < 8; j++) {
            testval = testval >> 1;
            if (*fontp & testval)
                *addr = FRcolor;
            else
                *addr = BKcolor;
            addr++;
        }
        fontp++;
    }
}

int32 skip_atoi(const char **s) {
    int32 i = 0;
    while (is_digit(**s))
        i = i * 10 + *((*s)++) - '0';
    return i;
}

/**
 * @brief 内部数字转字符串函数（加入 end 指针边界检查防溢出）
 */
static char *number(char *str, char *end, int64 num, int32 base, int32 size, int32 precision, int32 type) {
    char c, sign, tmp[50];
    const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int32 i;

    if (type & SMALL) digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (type & LEFT) type &= ~ZEROPAD;
    if (base < 2 || base > 36)
        return str;
    c = (type & ZEROPAD) ? '0' : ' ';
    sign = 0;
    if (type & SIGN && num < 0) {
        sign = '-';
        num = -num;
    } else
        sign = (type & PLUS) ? '+' : ((type & SPACE) ? ' ' : 0);
    if (sign) size--;
    if (type & SPECIAL) {
        if (base == 16) size -= 2;
        else if (base == 8) size--;
    }
    i = 0;
    if (num == 0)
        tmp[i++] = '0';
    else
        while (num != 0)
            tmp[i++] = digits[do_div(num, base)];
    if (i > precision) precision = i;
    size -= precision;

    if (!(type & (ZEROPAD | LEFT)))
        while (size-- > 0)
            if (str < end) *str++ = ' ';
    if (sign)
        if (str < end) *str++ = sign;
    if (type & SPECIAL) {
        if (base == 8) {
            if (str < end) *str++ = '0';
        } else if (base == 16) {
            if (str < end) *str++ = '0';
            if (str < end) *str++ = digits[33];
        }
    }
    if (!(type & LEFT))
        while (size-- > 0)
            if (str < end) *str++ = c;

    while (i < precision--)
        if (str < end) *str++ = '0';
    while (i-- > 0)
        if (str < end) *str++ = tmp[i];
    while (size-- > 0)
        if (str < end) *str++ = ' ';
    return str;
}

/**
 * @brief 安全的格式化写入函数（基于首尾指针控制，防止缓冲区溢出）
 */
int32 vsnprintf(char *buf, uint32 size, const char *fmt, va_list args) {
    char *str = buf;
    char *end = buf + size;
    char *s;
    int32 flags;
    int32 field_width;
    int32 precision;
    int32 len, i;
    int32 qualifier;

    if (size == 0) return 0;
    end--; // 预留 1 字节给字符串结束符 '\0'

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            if (str < end) *str++ = *fmt;
            continue;
        }
        flags = 0;
    repeat:
        fmt++;
        switch (*fmt) {
            case '-': flags |= LEFT; goto repeat;
            case '+': flags |= PLUS; goto repeat;
            case ' ': flags |= SPACE; goto repeat;
            case '#': flags |= SPECIAL; goto repeat;
            case '0': flags |= ZEROPAD; goto repeat;
        }

        /* 获取域宽 */
        field_width = -1;
        if (is_digit(*fmt))
            field_width = skip_atoi(&fmt);
        else if (*fmt == '*') {
            fmt++;
            field_width = va_arg(args, int);
            if (field_width < 0) {
                field_width = -field_width;
                flags |= LEFT;
            }
        }

        /* 获取精度 */
        precision = -1;
        if (*fmt == '.') {
            fmt++;
            if (is_digit(*fmt))
                precision = skip_atoi(&fmt);
            else if (*fmt == '*') {
                fmt++;
                precision = va_arg(args, int);
            }
            if (precision < 0) precision = 0;
        }

        qualifier = -1;
        if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L' || *fmt == 'Z') {
            qualifier = *fmt;
            fmt++;
        }

        switch (*fmt) {
            case 'c':
                if (!(flags & LEFT))
                    while (--field_width > 0)
                        if (str < end) *str++ = ' ';
                if (str < end) *str++ = (unsigned char) va_arg(args, int);
                while (--field_width > 0)
                    if (str < end) *str++ = ' ';
                break;

            case 's':
                s = va_arg(args, char *);
                if (!s) s = "(null)"; // 防止空指针异常
                len = asm_strlen(s);
                if (precision < 0) precision = len;
                else if (len > precision) len = precision;

                if (!(flags & LEFT))
                    while (len < field_width--)
                        if (str < end) *str++ = ' ';
                for (i = 0; i < len; i++)
                    if (str < end) *str++ = *s++;
                while (len < field_width--)
                    if (str < end) *str++ = ' ';
                break;

            case 'o':
                if (qualifier == 'l')
                    str = number(str, end, va_arg(args, unsigned long), 8, field_width, precision, flags);
                else
                    str = number(str, end, va_arg(args, uint32), 8, field_width, precision, flags);
                break;

            case 'p':
                if (field_width == -1) {
                    field_width = 2 * sizeof(void *);
                    flags |= ZEROPAD;
                }
                str = number(str, end, (unsigned long) va_arg(args, void *), 16, field_width, precision, flags);
                break;

            case 'x':
                flags |= SMALL;
            case 'X':
                if (qualifier == 'l')
                    str = number(str, end, va_arg(args, unsigned long), 16, field_width, precision, flags);
                else
                    str = number(str, end, va_arg(args, uint32), 16, field_width, precision, flags);
                break;

            case 'd':
            case 'i':
                flags |= SIGN;
            case 'u':
                if (qualifier == 'l')
                    str = number(str, end, va_arg(args, unsigned long), 10, field_width, precision, flags);
                else
                    str = number(str, end, va_arg(args, uint32), 10, field_width, precision, flags);
                break;

            case 'n':
                if (qualifier == 'l') {
                    int64 *ip = va_arg(args, int64 *);
                    *ip = (str - buf);
                } else {
                    int32 *ip = va_arg(args, int32 *);
                    *ip = (str - buf);
                }
                break;

            case '%':
                if (str < end) *str++ = '%';
                break;

            default:
                if (str < end) *str++ = '%';
                if (*fmt) {
                    if (str < end) *str++ = *fmt;
                } else {
                    fmt--;
                }
                break;
        }
    }
    *str = '\0';
    return str - buf;
}

/**
 * @brief 帧缓冲终端向上滚屏 1 行
 */
static void console_scroll_up(uint32 BKcolor) {
    uint64 pixels_per_row = (unsigned long)Pos.PixelsPerScanLine * Pos.YCharSize;
    int32 max_lines = Pos.YResolution / Pos.YCharSize;
    uint64 copy_pixels = pixels_per_row * (max_lines - 1);

    uint32 *dest = Pos.FB_addr;
    uint32 *src  = Pos.FB_addr + pixels_per_row;

    // 将显存向上平移 1 行
    asm_mem_cpy(dest,src,  copy_pixels * sizeof(uint32));

    // 用背景色填满底部空出的最后一行
    uint32 *last_line = Pos.FB_addr + copy_pixels;
    for (uint64 i = 0; i < pixels_per_row; i++) {
        last_line[i] = BKcolor;
    }

    Pos.YPosition = max_lines - 1;
}

/**
 * @brief 内核彩色打印核心接口（采用局部栈缓冲区，支持多核 SMP 并发安全）
 */
int32 color_printk(uint32 FRcolor, uint32 BKcolor, const char *fmt, ...) {
    char buf[1024]; // 局域栈缓存，彻底解决多核全局变量覆盖冲突
    int32 i = 0;
    int32 count = 0;
    int32 line = 0;
    va_list args;

    va_start(args, fmt);
    i = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (i <= 0) return i;

    spin_lock(&Pos.lock);

    for (count = 0; count < i || line; count++) {
        if (line > 0) {
            count--;
            goto Label_tab;
        }

        unsigned char c = (unsigned char)*(buf + count);

        if (c == '\n') {
            Pos.YPosition++;
            Pos.XPosition = 0;
        } else if (c == '\b') {
            Pos.XPosition--;
            if (Pos.XPosition < 0) {
                Pos.XPosition = (Pos.XResolution / Pos.XCharSize) - 1;
                Pos.YPosition--;
                if (Pos.YPosition < 0)
                    Pos.YPosition = (Pos.YResolution / Pos.YCharSize) - 1;
            }
            putchar(Pos.FB_addr, Pos.PixelsPerScanLine, Pos.XPosition * Pos.XCharSize,
                    Pos.YPosition * Pos.YCharSize, FRcolor, BKcolor, ' ');
        } else if (c == '\t') {
            line = ((Pos.XPosition + 8) & ~(8 - 1)) - Pos.XPosition;

        Label_tab:
            line--;
            putchar(Pos.FB_addr, Pos.PixelsPerScanLine, Pos.XPosition * Pos.XCharSize,
                    Pos.YPosition * Pos.YCharSize, FRcolor, BKcolor, ' ');
            Pos.XPosition++;
        } else {
            putchar(Pos.FB_addr, Pos.PixelsPerScanLine, Pos.XPosition * Pos.XCharSize,
                    Pos.YPosition * Pos.YCharSize, FRcolor, BKcolor, c);
            Pos.XPosition++;
        }

        /* 自动折行处理 */
        if (Pos.XPosition >= (Pos.XResolution / Pos.XCharSize)) {
            Pos.YPosition++;
            Pos.XPosition = 0;
        }

        /* 屏幕底部越界自动上滚屏 */
        if (Pos.YPosition >= (Pos.YResolution / Pos.YCharSize)) {
            console_scroll_up(BKcolor);
        }
    }

    spin_unlock(&Pos.lock);
    return i;
}

/**
 * @brief 系统调用：彩色打印（统一采用 cpu_registers_t *regs 签名）
 * @param regs 进内核时保存的用户态寄存器现场
 */
int64 sys_color_printk(cpu_registers_t *regs) {
    // 从寄存器中按 ABI 约定提取参数：
    // rdi = 前景色 (FRcolor)
    // rsi = 背景色 (BKcolor)
    // rdx = 用户态字符串指针 (user_str)
    uint32 FRcolor = (uint32)regs->rdi;
    uint32 BKcolor = (uint32)regs->rsi;
    const char *user_str   = (const char *)regs->rdx;

    char kernel_buf[512];

    // 1. 安全边界检查：确保用户指针在合法的低半区用户空间范围内
    if ((unsigned long)user_str >= 0x00007FFFFFFFFFFFULL) {
        return -1; // 非法地址，拒绝访问
    }

    // 2. 从用户空间安全复制字符串到内核栈缓冲区（带截断保护防范越界）
    int i = 0;
    while (i < sizeof(kernel_buf) - 1) {
        char c = user_str[i];
        kernel_buf[i] = c;
        if (c == '\0') break;
        i++;
    }
    kernel_buf[sizeof(kernel_buf) - 1] = '\0';

    // 3. 调用底层的安全打印函数输出到屏幕
    return color_printk(FRcolor, BKcolor, "%s", kernel_buf);
}
