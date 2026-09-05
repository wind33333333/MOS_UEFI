#pragma once

#include "../include/moslib.h"
#include "../include/vmm_page.h"

void kpage_table_init(void);

extern vm_space_t kernel_space;
