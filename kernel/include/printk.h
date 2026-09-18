#pragma once
#include "moslib.h"

#define RED	    0x00ff0000		//红
#define GREEN	0x0000ff00		//绿
#define BLUE	0x000000ff		//蓝
#define WHITE 	0x00ffffff		//白
#define BLACK 	0x00000000		//黑
#define ORANGE	0x00ff8000		//橙
#define YELLOW	0x00ffff00		//黄
#define CYAN	0x0000ffff		//青
#define PURPLE	0x008000ff		//紫

typedef struct position_t {
    uint32 XResolution;
    uint32 YResolution;
    uint32 PixelsPerScanLine;

    uint32 XPosition;
    uint32 YPosition;

    uint32 XCharSize;
    uint32 YCharSize;

    uint32* FB_addr;
    uint64 FB_length;
    uint32 lock;
}position_t;

int32 color_printk(unsigned int FRcolor,unsigned int BKcolor,const char * fmt,...);

#define PR_INFO(fmt, ...)   color_printk(CYAN,   BLACK, "[ INFO ] " fmt, ##__VA_ARGS__)
#define PR_OK(fmt, ...)     color_printk(GREEN,  BLACK, "[  OK  ] " fmt, ##__VA_ARGS__)
#define PR_WARN(fmt, ...)   color_printk(YELLOW, BLACK, "[ WARN ] " fmt, ##__VA_ARGS__)
#define PR_ERROR(fmt, ...)  color_printk(RED,    BLACK, "[ ERROR] " fmt, ##__VA_ARGS__)







