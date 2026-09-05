#pragma once
#include "moslib.h"



uint32 apicid_to_cpuid(uint32 apic_id);
uint32 cpuid_to_apicid(uint32 cpu_id);
void bsp_init(void);
void enable_cpu_advanced_features(void);

typedef struct {
    char8 manufacturer_name[13];
    char8 model_name[49];
    uint32 logical_processors_number;
    uint32 fundamental_hz;
    uint32 maximum_hz;
    uint32 bus_hz;
    uint32 tsc_hz;
}cpu_info_t;

extern cpu_info_t cpu_info;
extern uint32 *apic_id_table;

void ap_init(void);
void ap_main(void);

typedef struct {
    uint64 rsp;
    uint16 tr;
}__attribute__((packed)) apboot_data_t;

extern uint8 _apboot_start[];
extern uint8 _apboot_end[];

extern uint64 ap_rsp_ptr;
extern void *ap_main_ptr;
extern uint64* ap_tmp_pml4t_ptr;
extern uint32 *apic_id_table_ptr;
extern uint64 ap_boot_loader_address;
