#ifndef __TSS_INIT_H__
#define __TSS_INIT_H__
#include "moslib.h"
#include "memory.h"
#include "gdt.h"

#define SET_TSS_L(BASE)  \
    (TSS_TYPE | P | TSS_LIMIT | DPL_0 | \
    (((unsigned long)(BASE) & 0x000000000000FFFF) << 16) | \
    (((unsigned long)(BASE) >> 16) & 0x00000000000000FF) << 32 | \
    (((unsigned long)(BASE) >> 24) & 0x00000000000000FF) << 56)

#define SET_TSS_H(BASE)  ((unsigned long)(BASE) >> 32)

#define SET_TSS(GDTBASE,NUM,BASE) \
               do { \
               GDTBASE[NUM*2] = SET_TSS_L(BASE); \
               GDTBASE[NUM*2+1] = SET_TSS_H(BASE); \
               }while(0)


#define TSS_TYPE    (0x9UL << 40)
#define TSS_LIMIT   (0x67UL & 0xFFFF) | ((0x67UL >> 16)<<48)

void tssInit(UINT32 cpuId,UINT8 bspFlags);

typedef struct {
    UINT32    reserved0;
    unsigned long   rsp0;
    unsigned long   rsp1;
    unsigned long   rsp2;
    unsigned long   reserved1;
    unsigned long   ist1;
    unsigned long   ist2;
    unsigned long   ist3;
    unsigned long   ist4;
    unsigned long   ist5;
    unsigned long   ist6;
    unsigned long   ist7;
    unsigned long   reserved2;
    UINT16  reserved3;
    UINT16  iomap_base;
} __attribute__((packed)) _tss;

typedef struct {
    unsigned long limit;
    _tss * base;
} _tss_ptr;

__attribute__((section(".init_data"))) _tss_ptr tss_ptr = {0,0};

#endif