#pragma once

#include "../include/moslib.h"
#include "../include/vmm_page.h"

void init_kpage_table(void);

extern vm_space_t kernel_space;
