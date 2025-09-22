#pragma once

#include "moslib.h"

//TSS起始选择子
#define TSS_DESCRIPTOR_START_INDEX 5

#define TSS_TYPE    (0x9UL << 40)
#define TSS_LIMIT   (0x67UL & 0xFFFF) | ((0x67UL >> 16)<<48)

void init_tss(void);

#pragma pack(push,1)

typedef struct {
    uint32   reserved0;
    uint64   rsp0;
    uint64   rsp1;
    uint64   rsp2;
    uint64   reserved1;
    uint64   ist1;
    uint64   ist2;
    uint64   ist3;
    uint64   ist4;
    uint64   ist5;
    uint64   ist6;
    uint64   ist7;
    uint64   reserved2;
    uint16   reserved3;
    uint16   iomap_base;
} tss_t;

#pragma pack(pop)