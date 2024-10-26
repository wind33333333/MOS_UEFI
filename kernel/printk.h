#ifndef __PRINTK_H__
#define __PRINTK_H__

#include <stdarg.h>
#include "font.h"
#include "linkage.h"
#include "moslib.h"


void init_output(UINT8 bsp_flags);
void clear_screen(void);
extern char buf[4096];


#define ZEROPAD	1		/* pad with zero */
#define SIGN	2		/* unsigned/signed long */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define LEFT	16		/* left justified */
#define SPECIAL	32		/* 0x */
#define SMALL	64		/* use 'abcdef' instead of 'ABCDEF' */

#define is_digit(c)	((c) >= '0' && (c) <= '9')

#define WHITE 	0x00ffffff		//白
#define BLACK 	0x00000000		//黑
#define RED	    0x00ff0000		//红
#define ORANGE	0x00ff8000		//橙
#define YELLOW	0x00ffff00		//黄
#define GREEN	0x0000ff00		//绿
#define BLUE	0x000000ff		//蓝
#define INDIGO	0x0000ffff		//靛
#define PURPLE	0x008000ff		//紫

/*

*/

extern UINT8 font_ascii[256][16];

struct position
{
	UINT32 XResolution;
	UINT32 YResolution;
    UINT32 PixelsPerScanLine;

	UINT32 XPosition;
	UINT32 YPosition;

	UINT32 XCharSize;
	UINT32 YCharSize;

	UINT32* FB_addr;
	UINT64 FB_length;
    char lock;
}Pos;

/*

*/

void putchar(UINT32 *fb, UINT32 Xsize, UINT32 x, UINT32 y, UINT32 FRcolor, UINT32 BKcolor,UINT8 font);

/*

*/

UINT32 skip_atoi(const char **s);

/*

*/

#define do_div(n,base) ({ \
UINT32 __res; \
__asm__("divq %%rcx":"=a" (n),"=d" (__res):"0" (n),"1" (0),"c" (base)); \
__res; })

/*

*/

static char * number(char * str, long num, UINT32 base, UINT32 size, UINT32 precision ,UINT32 type);

/*

*/

UINT32 vsprintf(char * buf,const char *fmt, va_list args);

/*

*/

UINT32 color_printk(UINT32 FRcolor,UINT32 BKcolor,const char * fmt,...);

#endif

