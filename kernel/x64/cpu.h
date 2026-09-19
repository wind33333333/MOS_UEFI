#pragma once
#include "moslib.h"

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

uint32 apicid_to_cpuid(uint32 apic_id);
uint32 cpuid_to_apicid(uint32 cpu_id);

