#include "cpu.h"

cpu_info_t cpu_info;
uint32 *apic_id_table; //apic_id_table


uint32 apicid_to_cpuid(uint32 apic_id) {
    for (uint32 i = 0; i < cpu_info.logical_processors_number; i++) {
        if (apic_id == apic_id_table[i])
            return i;
    }
    return 0xFFFFFFFF;
}

uint32 cpuid_to_apicid(uint32 cpu_id) {
    return apic_id_table[cpu_id];
}

