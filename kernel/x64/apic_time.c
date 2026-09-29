#include "apic_time.h"

#include "cpu.h"
#include "interrupt.h"
#include "msr.h"


tsc_clock_t       g_tsc_clock;
apic_timer_cfg_t  g_apic_timer;

