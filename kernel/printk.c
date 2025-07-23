#include "printk.h"
#include "vmm.h"
#include "uefi.h"
#include "vmalloc.h"

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
                len = strlen(s);
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


        if (Pos.XPosition >= (Pos.XResolution / Pos.XCharSize)) {
            Pos.YPosition++;
            Pos.XPosition = 0;
        }
        if (Pos.YPosition >= (Pos.YResolution / Pos.YCharSize)) {
            Pos.YPosition = 0;
        }
    }
    Pos.lock = 0; //解锁
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
    Pos.FB_addr = boot_info->frame_buffer_base;
    Pos.FB_length = boot_info->frame_buffer_size;
    Pos.lock = 0;
    clear_screen();

    color_printk(GREEN, BLACK, "voide memory phyaddr:%#lx size:%#lx",Pos.FB_addr,Pos.FB_length);
}

INIT_TEXT void video_mem_map(void) {
    UINT64 page_size,attr;
    if (boot_info->frame_buffer_size >= PAGE_1G_SIZE) {
        page_size = PAGE_1G_SIZE;
        attr = PAGE_ROOT_RW_WC_2M1G;
    }else if (boot_info->frame_buffer_size >= PAGE_2M_SIZE) {
        page_size = PAGE_2M_SIZE;
        attr = PAGE_ROOT_RW_WC_2M1G;
    }else {
        page_size = PAGE_4K_SIZE;
        attr = PAGE_ROOT_RW_WC_4K;
    }
    Pos.FB_addr = iomap(Pos.FB_addr,Pos.FB_length,page_size,attr);
}

void clear_screen(void) {
    for (UINT64 i = 0; i < (Pos.PixelsPerScanLine * Pos.YResolution); i++) {
        *((UINT32 *) Pos.FB_addr + i) = BLACK;
    }
    Pos.XPosition = 0;
    Pos.YPosition = 0;
}
