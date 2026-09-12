#pragma once

#include "moslib.h"
#include "../include/vmm.h"

// =========================================================================
// 核心 API 声明
// =========================================================================

void vmalloc_init(void);
void *vmalloc(uint64 size);
void vfree(void *ptr);

void *_ioremap(uint64 start_pa, uint64 size, uint64 flags);
int32 ioreunmap(void *ptr);

void *ioremap(uint64 start_pa, uint64 size);
void *ioremap_wc(uint64 start_pa, uint64 size);

void *memremap(uint64 start_pa, uint64 size);
int32 unmemremap(void *ptr);

void *module_remap(uint64 start_pa, uint64 size);
int32 unmodule_remap(void *ptr);

// 内存权限动态修饰接口
int32 _set_memory_flags(uint64 vaddr, uint64 size, uint64 flags);
static inline int32 set_memory_ro(uint64 vaddr, uint64 size) { return _set_memory_flags(vaddr, size, HW_PAGE_NX); }
static inline int32 set_memory_rw(uint64 vaddr, uint64 size) { return _set_memory_flags(vaddr, size, HW_PAGE_NX | HW_PAGE_RW); }
static inline int32 set_memory_rx(uint64 vaddr, uint64 size) { return _set_memory_flags(vaddr, size, 0); }
static inline int32 set_memory_rwx(uint64 vaddr, uint64 size){ return _set_memory_flags(vaddr, size, HW_PAGE_RW); }

