#pragma once
#include "moslib.h"
#include "list.h"

// 每个 CPU 独享 16 MB 的虚拟窗口
#define PERCPU_WINDOW_SIZE  (16 * 1024 * 1024)

#define PCPU_CHUNK_SIZE (64 * 1024) // 每次扩容的大小 (64KB)

// 🌟 物理载荷以外的“全局元数据块” (通过全局 kmalloc 分配)
typedef struct pcpu_block_t {
    list_head_t list;       // 链接到 Chunk 的块链表
    uint32      offset;     // 该块的全局相对偏移量
    uint32      size;       // 该块的大小 (字节)
    boolean     is_free;    // true 为空闲，false 为被占用
    boolean     is_dynamic;
} pcpu_block_t;

// 🌟 管理一次扩容的 Chunk 结构
typedef struct pcpu_chunk_t {
    list_head_t list;       // 链接到全局 chunk 链表
    uint32      start_off;  // 该 Chunk 管辖的起始偏移量 (如 0x0)
    uint32      end_off;    // 该 Chunk 管辖的结束偏移量 (如 0x10000)
    uint32      free_bytes; // 剩余空闲空间总和
    list_head_t block_list; // 有序挂载该 Chunk 内所有的 pcpu_block_t
} pcpu_chunk_t;

extern list_head_t pcpu_chunk_head;
extern uint32  pcpu_alloc_lock;

void per_cpu_mem_init();

void *alloc_percpu(uint32 size, uint32 align);
void free_percpu(void *ptr);