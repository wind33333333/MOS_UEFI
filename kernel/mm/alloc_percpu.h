#pragma once
#include "moslib.h"
#include "list.h"
#include "vmm.h"


// 每个 CPU 独享 16 MB 的虚拟窗口
#define PERCPU_WINDOW_SIZE  (16 * 1024 * 1024)

#define PCPU_CHUNK_SIZE (64 * 1024) // 每次扩容的大小 (64KB)

// 🌟 物理载荷以外的“全局元数据块” (通过全局 kmalloc 分配)
typedef struct pcpu_block_t {
    list_head_t node;       // 链接到 Chunk 的块链表
    uint32      offset;     // 该块的全局相对偏移量
    uint32      size;       // 该块的大小 (字节)
    boolean     is_free;    // true 为空闲，false 为被占用
    boolean     is_dynamic;
} pcpu_block_t;

// 🌟 管理一次扩容的 Chunk 结构
typedef struct pcpu_chunk_t {
    list_head_t node;       // 链接到全局 chunk 链表
    uint32      start_off;  // 该 Chunk 管辖的起始偏移量 (如 0x0)
    uint32      end_off;    // 该 Chunk 管辖的结束偏移量 (如 0x10000)
    uint32      free_bytes; // 剩余空闲空间总和
    list_head_t block_list; // 有序挂载该 Chunk 内所有的 pcpu_block_t
} pcpu_chunk_t;

/**
 * 🌟 跨核访问：通过计算物理窗口偏移，获取目标 CPU 的私有数据指针
 * @param ptr    通过 alloc_percpu 获取的全局相对偏移量指针
 * @param logical_id 目标 CPU 的核心编号
 * @return       目标 CPU 该变量的真实虚拟地址 (void* 自动适配接收类型)
 */
static inline void* per_cpu_ptr(const void* ptr, uint32 logical_id) {
    uint64 __va = vm_layout.percpu_vaddr_start +
                  (uint64)ptr +
                  (logical_id * PERCPU_WINDOW_SIZE);
    return (void*)__va;
}

void alloc_percpu_init();
void *alloc_percpu(uint32 size, uint32 align);
void free_percpu(void *ptr);
