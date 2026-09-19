#pragma once

#include "vmm_init.h"
#include "vmm.h"

/**
 * @brief 创建并初始化独立的虚拟地址空间
 * @param space        目标地址空间对象指针
 * @param level        分页级数 (4 或 5)
 * @param ops          底层操作回调集 (采用 const 指针传参，避开 32 字节结构体溢出 ABI 寄存器限制)
 * @param clone_kernel 是否共享克隆高半核的内核空间 (映射 PML4/5 顶层 256..511 项)
 * @return vm_status_e 执行状态
 */
vm_status_e vm_space_init(vm_space_t *space, uint8 level, const vm_allocator_ops_t *ops, boolean clone_kernel);