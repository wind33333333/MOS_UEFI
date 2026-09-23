#pragma once
#include "moslib.h"

typedef struct {
    uint32   reserved0;
    uint64    rsp0;
    uint64    rsp1;
    uint64    rsp2;
    uint64   reserved1;
    uint64    ist1;
    uint64    ist2;
    uint64    ist3;
    uint64    ist4;
    uint64    ist5;
    uint64    ist6;
    uint64    ist7;
    uint64   reserved2;
    uint16   reserved3;
    uint16   iomap_base;
} __attribute__((packed)) tss_t;



void gdt_tss_init(uint32 logical_id);

