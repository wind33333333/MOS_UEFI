#ifndef __TSS_INIT_H__
#define __TSS_INIT_H__
#include "moslib.h"

#define SET_TSS_L(BASE)  \
    (TSS_TYPE | P | TSS_LIMIT | DPL_0 | \
    (((UINT64)(BASE) & 0x000000000000FFFF) << 16) | \
    (((UINT64)(BASE) >> 16) & 0x00000000000000FF) << 32 | \
    (((UINT64)(BASE) >> 24) & 0x00000000000000FF) << 56)

#define SET_TSS_H(BASE)  ((UINT64)(BASE) >> 32)

#define SET_TSS(GDTBASE,NUM,BASE) \
               do { \
               GDTBASE[NUM*2] = SET_TSS_L(BASE); \
               GDTBASE[NUM*2+1] = SET_TSS_H(BASE); \
               }while(0)


#define TSS_TYPE    (0x9UL << 40)
#define TSS_LIMIT   (0x67UL & 0xFFFF) | ((0x67UL >> 16)<<48)

void init_tss(UINT32 cpu_id,UINT8 bsp_flags);

typedef struct {
    UINT32   reserved0;
    UINT64   rsp0;
    UINT64   rsp1;
    UINT64   rsp2;
    UINT64   reserved1;
    UINT64   ist1;
    UINT64   ist2;
    UINT64   ist3;
    UINT64   ist4;
    UINT64   ist5;
    UINT64   ist6;
    UINT64   ist7;
    UINT64   reserved2;
    UINT16   reserved3;
    UINT16   iomap_base;
} __attribute__((packed)) tss_t;

#endif