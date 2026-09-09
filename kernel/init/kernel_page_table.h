#pragma once

#include "../include/moslib.h"
#include "../include/vmm.h"

void kpage_table_init(void);

extern vm_space_t kernel_space;
