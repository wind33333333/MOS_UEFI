#ifndef __TSS_INIT_H__
#define __TSS_INIT_H__
#include "moslib.h"

//TSS起始选择子
#define TSS_DESCRIPTOR_START_INDEX 5

#define TSS_TYPE    (0x9UL << 40)
#define TSS_LIMIT   (0x67UL & 0xFFFF) | ((0x67UL >> 16)<<48)

void init_tss(void);
void set_tss_descriptor(UINT32 index,UINT64 tss_address);

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

static inline void ltr(UINT16 tss_sel) {
    __asm__ __volatile__(
            "ltr    %w0 \n\t"
            :
            : "r"(tss_sel)
            :
            );
}

#endif