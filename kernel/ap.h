#ifndef __SMP_INIT__
#define __SMP_INIT__
#include "moslib.h"

void init_ap(void);
void ap_main(void);

typedef struct {
    UINT64 rsp;
    UINT16 tr;
}__attribute__((packed)) apboot_data_t;

extern UINT8 _apboot_start[];
extern UINT8 _apboot_end[];

//extern UINT64 ap_rsp;
extern UINT64 ap_rsp_ptr;
extern void *ap_main_ptr;
extern UINT64* ap_tmp_pml4t_ptr;
extern UINT32 *apic_id_table_ptr;
extern UINT64 ap_boot_loader_address;

#endif