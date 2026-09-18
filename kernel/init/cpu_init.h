#pragma once

#include "moslib.h"
#include "cpu.h"


typedef struct {
    uint64 rsp;
    uint16 tr;
}__attribute__((packed)) apboot_data_t;

extern cpu_info_t cpu_info;
extern uint32 *apic_id_table;

extern uint8 _apboot_start[];
extern uint8 _apboot_end[];

extern uint64 ap_rsp_ptr;
extern void *ap_main_ptr;
extern uint64* ap_tmp_pml4t_ptr;
extern uint32 *apic_id_table_ptr;
extern uint64 ap_boot_loader_address;

void ap_init(void);
void ap_main(void);
void bsp_init(void);
uint64 cpu_feature_init(void);
