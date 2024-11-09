#ifndef __SMP_INIT__
#define __SMP_INIT__
#include "moslib.h"

void init_ap(void);

typedef struct {
    UINT64 rsp;
    UINT16 tr;
}__attribute__((packed)) apboot_data_t;

extern UINT8 _apboot_start[];
extern UINT8 _apboot_end[];

extern UINT64 ap_rsp;
extern UINT64 ap_boot_loader_address;

#endif