#pragma once

#include "vmm_init.h"
#include "vmm.h"

// 初始化系统内存布局
void vm_layout_init(void);
void* tmp_directly_map(uint64 pa, uint64 size, uint64* page_table_buffer);