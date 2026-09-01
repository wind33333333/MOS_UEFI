#include "printk.h"
#include "../init/uefi.h"
#include "vmalloc.h"
#include "../include/font.h"

void putchar(unsigned int *fb, int Xsize, int x, int y, unsigned int FRcolor, unsigned int BKcolor,
             unsigned char font) {
    int i = 0, j = 0;
    unsigned int *addr = NULL;
    unsigned char *fontp = NULL;
    int testval = 0;
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

int skip_atoi(const char **s) {
    int i = 0;

    while (is_digit(**s))
        i = i * 10 + *((*s)++) - '0';
    return i;
}

static char *number(char *str, long num, int base, int size, int precision, int type) {
    char c, sign, tmp[50];
    const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int i;

    if (type & SMALL) digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (type & LEFT) type &= ~ZEROPAD;
    if (base < 2 || base > 36)
        return 0;
    c = (type & ZEROPAD) ? '0' : ' ';
    sign = 0;
    if (type & SIGN && num < 0) {
        sign = '-';
        num = -num;
    } else
        sign = (type & PLUS) ? '+' : ((type & SPACE) ? ' ' : 0);
    if (sign) size--;
    if (type & SPECIAL)
        if (base == 16) size -= 2;
        else if (base == 8) size--;
    i = 0;
    if (num == 0)
        tmp[i++] = '0';
    else
        while (num != 0)
            tmp[i++] = digits[do_div(num, base)];
    if (i > precision) precision = i;
    size -= precision;
    if (!(type & (ZEROPAD + LEFT)))
        while (size-- > 0)
            *str++ = ' ';
    if (sign)
        *str++ = sign;
    if (type & SPECIAL)
        if (base == 8)
            *str++ = '0';
        else if (base == 16) {
            *str++ = '0';
            *str++ = digits[33];
        }
    if (!(type & LEFT))
        while (size-- > 0)
            *str++ = c;

    while (i < precision--)
        *str++ = '0';
    while (i-- > 0)
        *str++ = tmp[i];
    while (size-- > 0)
        *str++ = ' ';
    return str;
}

int vsprintf(char *buf, const char *fmt, va_list args) {
    char *str, *s;
    int flags;
    int field_width;
    int precision;
    int len, i;

    int qualifier; /* 'h', 'l', 'L' or 'Z' for integer fields */

    for (str = buf; *fmt; fmt++) {
        if (*fmt != '%') {
            *str++ = *fmt;
            continue;
        }
        flags = 0;
    repeat:
        fmt++;
        switch (*fmt) {
            case '-':
                flags |= LEFT;
                goto repeat;
            case '+':
                flags |= PLUS;
                goto repeat;
            case ' ':
                flags |= SPACE;
                goto repeat;
            case '#':
                flags |= SPECIAL;
                goto repeat;
            case '0':
                flags |= ZEROPAD;
                goto repeat;
        }

        /* get field width */

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

        /* get the precision */

        precision = -1;
        if (*fmt == '.') {
            fmt++;
            if (is_digit(*fmt))
                precision = skip_atoi(&fmt);
            else if (*fmt == '*') {
                fmt++;
                precision = va_arg(args, int);
            }
            if (precision < 0)
                precision = 0;
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
                        *str++ = ' ';
                *str++ = (unsigned char) va_arg(args, int);
                while (--field_width > 0)
                    *str++ = ' ';
                break;

            case 's':

                s = va_arg(args, char *);
                if (!s)
                    s = '\0';
                len = asm_strlen(s);
                if (precision < 0)
                    precision = len;
                else if (len > precision)
                    len = precision;

                if (!(flags & LEFT))
                    while (len < field_width--)
                        *str++ = ' ';
                for (i = 0; i < len; i++)
                    *str++ = *s++;
                while (len < field_width--)
                    *str++ = ' ';
                break;

            case 'o':

                if (qualifier == 'l')
                    str = number(str, va_arg(args, unsigned long), 8, field_width, precision,
                                 flags);
                else
                    str = number(str, va_arg(args, unsigned int), 8, field_width, precision, flags);
                break;

            case 'p':

                if (field_width == -1) {
                    field_width = 2 * sizeof(void *);
                    flags |= ZEROPAD;
                }

                str = number(str, (unsigned long) va_arg(args, void *), 16, field_width, precision,
                             flags);
                break;

            case 'x':

                flags |= SMALL;

            case 'X':

                if (qualifier == 'l')
                    str = number(str, va_arg(args, unsigned long), 16, field_width, precision,
                                 flags);
                else
                    str = number(str, va_arg(args, unsigned int), 16, field_width, precision,
                                 flags);
                break;

            case 'd':
            case 'i':

                flags |= SIGN;
            case 'u':

                if (qualifier == 'l')
                    str = number(str, va_arg(args, unsigned long), 10, field_width, precision,
                                 flags);
                else
                    str = number(str, va_arg(args, unsigned int), 10, field_width, precision,
                                 flags);
                break;

            case 'n':

                if (qualifier == 'l') {
                    long *ip = va_arg(args, long *);
                    *ip = (str - buf);
                } else {
                    int *ip = va_arg(args, int *);
                    *ip = (str - buf);
                }
                break;

            case '%':

                *str++ = '%';
                break;

            default:

                *str++ = '%';
                if (*fmt)
                    *str++ = *fmt;
                else
                    fmt--;
                break;
        }
    }
    *str = '\0';
    return str - buf;
}

/**
 * @brief 帧缓冲终端向上滚屏 1 个字符行高度
 * @param BKcolor 滚屏后底部空白新行要填充的背景色
 */
static void console_scroll_up(unsigned int BKcolor) {
    // 1. 计算一整行字符占用的总像素点数
    unsigned long pixels_per_row = (unsigned long)Pos.PixelsPerScanLine * Pos.YCharSize;
    // 2. 计算屏幕垂直方向最多能容纳的字符行数
    int max_lines = Pos.YResolution / Pos.YCharSize;

    // 3. 需要向高地址偏移移动的像素总量（第 1 行到末行的像素总数）
    unsigned long copy_pixels = pixels_per_row * (max_lines - 1);

    unsigned int *dest = Pos.FB_addr;                  // 目标：第 0 行像素起始地址
    unsigned int *src  = Pos.FB_addr + pixels_per_row; // 源头：第 1 行像素起始地址

    /*
     * [拷贝旧画面]：将屏幕第 1 ~ 末行整体向上平移 1 行
     * 注意：由于是向上移动（src > dest），从前向后按顺序拷贝绝不会引发覆盖冲突
     * 如果你的内核里已经实现了优化的 asm_mem_cpy，这里强烈建议换成：
     * asm_mem_cpy(dest, src, copy_pixels * sizeof(unsigned int));
     */
    asm_mem_cpy(src,dest,copy_pixels*4);

    /*
     * [擦除底部新行]：将最后一行的行区域用背景色涂满
     */
    unsigned int *last_line = Pos.FB_addr + copy_pixels;
    for (unsigned long i = 0; i < pixels_per_row; i++) {
        last_line[i] = BKcolor;
    }

    // 4. 将光标 Y 轴拉回到屏幕最底下的最后一有效行
    Pos.YPosition = max_lines - 1;
}

int color_printk(unsigned int FRcolor, unsigned int BKcolor, const char *fmt, ...) {
    spin_lock(&Pos.lock);
    int i = 0;
    int count = 0;
    int line = 0;
    va_list args;
    va_start(args, fmt);

    i = vsprintf(buf, fmt, args);

    va_end(args);

    for (count = 0; count < i || line; count++) {
        ////	add \n \b \t
        if (line > 0) {
            count--;
            goto Label_tab;
        }
        if ((unsigned char) *(buf + count) == '\n') {
            Pos.YPosition++;
            Pos.XPosition = 0;
        } else if ((unsigned char) *(buf + count) == '\b') {
            Pos.XPosition--;
            if (Pos.XPosition < 0) {
                Pos.XPosition = (Pos.XResolution / Pos.XCharSize - 1) * Pos.XCharSize;
                Pos.YPosition--;
                if (Pos.YPosition < 0)
                    Pos.YPosition = (Pos.YResolution / Pos.YCharSize - 1) * Pos.YCharSize;
            }
            putchar(Pos.FB_addr, Pos.PixelsPerScanLine, Pos.XPosition * Pos.XCharSize,
                    Pos.YPosition * Pos.YCharSize, FRcolor, BKcolor, ' ');
        } else if ((unsigned char) *(buf + count) == '\t') {
            line = ((Pos.XPosition + 8) & ~(8 - 1)) - Pos.XPosition;

        Label_tab:
            line--;
            putchar(Pos.FB_addr, Pos.PixelsPerScanLine, Pos.XPosition * Pos.XCharSize,
                    Pos.YPosition * Pos.YCharSize, FRcolor, BKcolor, ' ');
            Pos.XPosition++;
        } else {
            putchar(Pos.FB_addr, Pos.PixelsPerScanLine, Pos.XPosition * Pos.XCharSize,
                    Pos.YPosition * Pos.YCharSize, FRcolor, BKcolor,
                    (unsigned char) *(buf + count));
            Pos.XPosition++;
        }

        // 处理自动折行
        if (Pos.XPosition >= (Pos.XResolution / Pos.XCharSize)) {
            Pos.YPosition++;
            Pos.XPosition = 0;
        }

        // 【核心修改点】：当 Y 轴越界到达屏幕底部时，不再归零触发回绕，而是调用上滚屏函数
        if (Pos.YPosition >= (Pos.YResolution / Pos.YCharSize)) {
            console_scroll_up(BKcolor);
        }
    }
    Pos.lock = 0; // 解锁
    return i;
}


//全局变量buf
char buf[4096];

INIT_TEXT void init_output(void) {
    Pos.XResolution = boot_info->horizontal_resolution;
    Pos.YResolution = boot_info->vertical_resolution;
    Pos.PixelsPerScanLine = boot_info->pixels_per_scan_line;
    Pos.XPosition = 0;
    Pos.YPosition = 0;
    Pos.XCharSize = 8;
    Pos.YCharSize = 16;
    Pos.FB_addr = (uint32*)boot_info->frame_buffer_base;
    Pos.FB_length = boot_info->frame_buffer_size;
    Pos.lock = 0;
    clear_screen();
    color_printk(GREEN, BLACK, "Video Memory Physics Addr:%#lx Video Size:%#lx Resolution:%d * %d\n",Pos.FB_addr,Pos.FB_length,Pos.XResolution,Pos.YResolution);
}

INIT_TEXT void video_mem_map(void) {
    Pos.FB_addr = ioremap_wc((uint64)Pos.FB_addr,Pos.FB_length);
    color_printk(GREEN, BLACK, "Voide Memory Physics Address:%#lx -> Virtual Address:%#lx\n",boot_info->frame_buffer_base,Pos.FB_addr);
}

void clear_screen(void) {
    for (uint64 i = 0; i < (Pos.PixelsPerScanLine * Pos.YResolution); i++) {
        *((uint32 *) Pos.FB_addr + i) = BLACK;
    }
    Pos.XPosition = 0;
    Pos.YPosition = 0;
}
