#pragma once
#include "vmm.h"

extern vm_layout_t vm_layout;



void cpu_enable_feature(void);
void per_cpu_alloc_resources(void);
void per_cpu_init(void);
void ap_init(void);
void ap_main(void);
