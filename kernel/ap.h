#ifndef __SMP_INIT__
#define __SMP_INIT__
#include "moslib.h"

void init_ap(void);

typedef struct {
    UINT64 rsp;
    UINT16 tr;
}__attribute__((packed)) apboot_data_t;

#define APBOOT_ADDR 0x10000

extern UINT8 _apboot_start;
extern UINT8 _apboot_end;

extern UINT32 init_cpu_num;
extern UINT64 ap_rsp;

#endif