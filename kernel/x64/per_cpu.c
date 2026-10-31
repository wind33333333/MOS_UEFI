#include "per_cpu.h"
#include "cpu.h"
#include "../init/apic_init.h"
#include "vmm.h"
#include "pmm.h"
#include "printk.h"
#include "slub.h"
#include "list.h"

extern cpu_apic_info_t cpu_apic_info;
extern vm_space_t kernel_space;

static list_head_t chunk_list;
static uint32      pcpu_alloc_lock;

// =========================================================================
// 🌟 核心巨变：256-bit 极速位图 (掌控 16MB 窗口内的 256 个 64KB Chunk)
// =========================================================================
// 4 个 uint64_t 刚好 256 bit。bit 位为 0 表示 Chunk 虚拟地址空闲，1 表示已分配
static uint64 pcpu_chunk_bitmap[4] = {0};

static pcpu_chunk_t first_chunk;       // 第一个大块手动分配 (占据 bit 0)
static pcpu_block_t pcpu_cpu_block;    // 第一个小块手动分配，存放per_cpu结构
static pcpu_block_t free_block;        // 第一个手动拆下来的空闲块

// -------------------------------------------------------------------------
// 位图辅助函数：寻找第一个为 0 的 bit，并将其置为 1 (O(1) 极速寻址)
// -------------------------------------------------------------------------
static inline int find_first_zero_bit_and_set(void) {
    for (int i = 0; i < 4; i++) {
        if (pcpu_chunk_bitmap[i] != 0xFFFFFFFFFFFFFFFFULL) {
            // __builtin_ctzll 是 GCC/Clang 内建函数，直接映射 x86 的 BSF/TZCNT 硬件指令
            // ~ 操作将 0 变 1，ctzll 会光速找到最低位的 1 的位置
            int bit = __builtin_ctzll(~pcpu_chunk_bitmap[i]);
            pcpu_chunk_bitmap[i] |= (1ULL << bit); // 将其标记为已用
            return (i * 64) + bit; // 返回全局 Chunk 索引 (0 ~ 255)
        }
    }
    return -1; // 16MB 的 Per-CPU 虚拟空间已彻底耗尽
}

// -------------------------------------------------------------------------
// 位图辅助函数：回收 Chunk，将对应的 bit 清零
// -------------------------------------------------------------------------
static inline void clear_chunk_bit(uint32 chunk_idx) {
    int array_idx = chunk_idx / 64;
    int bit_idx   = chunk_idx % 64;
    pcpu_chunk_bitmap[array_idx] &= ~(1ULL << bit_idx);
}


void per_cpu_mem_init() {
    uint32 total_cpu_count = cpu_apic_info.total_cpu_count;
    cpu_info.total_cpu_count = total_cpu_count;
    uint64 per_cpu_va = vm_layout.percpu_vaddr_start;

    for (uint32 i = 0; i < total_cpu_count; i++) {
        uint64 pa = page_to_pa(alloc_pages(4));
        vm_map_range(&kernel_space, per_cpu_va, pa, PCPU_CHUNK_SIZE, PAGE_KERNEL_DATA_RW);
        per_cpu_va += PERCPU_WINDOW_SIZE;
    }

    list_head_init(&chunk_list);
    list_head_init(&first_chunk.block_list);

    // 🌟 创世 Chunk 永远占据 offset 0，所以必须在位图中把第 0 位锁死 (置 1)
    pcpu_chunk_bitmap[0] = 1ULL;

    // 手动创建第一个 chunk
    first_chunk.start_off = 0;
    first_chunk.end_off = PCPU_CHUNK_SIZE;
    first_chunk.free_bytes = PCPU_CHUNK_SIZE - sizeof(per_cpu_t);
    list_add_tail(&first_chunk.node, &chunk_list);

    // 手动创建 per_cpu 的 block
    pcpu_cpu_block.is_free = FALSE;
    pcpu_cpu_block.is_dynamic = FALSE;
    pcpu_cpu_block.offset = 0;
    pcpu_cpu_block.size = sizeof(per_cpu_t);
    list_add_tail(&pcpu_cpu_block.node, &first_chunk.block_list);

    // 手动创建第一个空闲 block
    free_block.is_free = TRUE;
    free_block.is_dynamic = FALSE;
    free_block.offset = sizeof(per_cpu_t);
    free_block.size = PCPU_CHUNK_SIZE - sizeof(per_cpu_t);
    list_add_tail(&free_block.node, &first_chunk.block_list);
}


// 🌟 扩容函数：创造一个新的 Chunk 并映射给所有 CPU
static inline pcpu_chunk_t *pcpu_expand_chunk(void) {
    // 1. O(1) 极速从位图中分配一个绝对空闲的 64KB 虚拟空间索引
    int chunk_idx = find_first_zero_bit_and_set();
    if (chunk_idx < 0) {
        PR_WARN("Per-CPU 16MB vaddr window exhausted!");
        return NULL; // OOM 拦截防线
    }

    // 2. 创建全局 Chunk 控制块
    pcpu_chunk_t *new_chunk = kmalloc(sizeof(pcpu_chunk_t));
    if (!new_chunk) {
        clear_chunk_bit(chunk_idx); // 内存不足，退还位图
        return NULL;
    }

    // 🌟 纯数学映射算出 offset，彻底告别 global_max_offset！
    new_chunk->start_off  = chunk_idx * PCPU_CHUNK_SIZE;
    new_chunk->end_off    = new_chunk->start_off + PCPU_CHUNK_SIZE;
    new_chunk->free_bytes = PCPU_CHUNK_SIZE;
    list_head_init(&new_chunk->block_list);

    uint64 per_cpu_va = vm_layout.percpu_vaddr_start + new_chunk->start_off;

    // 3. 为系统中的【每一个】CPU 分配真实的物理页，并映射到连续的虚拟空间
    for (uint32 i = 0; i < cpu_info.total_cpu_count; i++) {
        uint64 pa = page_to_pa(alloc_pages(4));
        vm_map_range(&kernel_space, per_cpu_va, pa, PCPU_CHUNK_SIZE, PAGE_KERNEL_DATA_RW);
        per_cpu_va += PERCPU_WINDOW_SIZE;
    }

    // 4. 创建一个涵盖整个新 Chunk 的初始空闲大块
    pcpu_block_t *new_block = kmalloc(sizeof(pcpu_block_t));
    new_block->offset     = new_chunk->start_off;
    new_block->size       = PCPU_CHUNK_SIZE;
    new_block->is_free    = TRUE;
    new_block->is_dynamic = TRUE;
    list_add_tail(&new_block->node, &new_chunk->block_list);

    // 5. 将新 Chunk 加入全局链表
    list_add(&new_chunk->node, &chunk_list);

    return new_chunk;
}

// =========================================================================
// 🌟 进阶：空闲 Chunk 终极回收引擎 (结合 Bitmap)
// =========================================================================
static inline void pcpu_recycle_empty_chunk(pcpu_chunk_t *chunk) {
    // 1. 阈值与防御性拦截：必须全空，且绝对不能是创世 Chunk
    if (chunk->free_bytes != PCPU_CHUNK_SIZE || chunk == &first_chunk) {
        return;
    }

    // 2. 释放底层物理页，并解除 VMM 映射
    for (uint32 i = 0; i < cpu_info.total_cpu_count; i++) {
        uint64 per_cpu_va = vm_layout.percpu_vaddr_start + chunk->start_off + (i * PERCPU_WINDOW_SIZE);
        vm_unmap_range(&kernel_space, per_cpu_va, PCPU_CHUNK_SIZE, UNMAP_FLAG_FREE_PHYS);
    }

    // 3. 销毁残留的空闲 Block 元数据
    pcpu_block_t *last_block = CONTAINER_OF(chunk->block_list.next, pcpu_block_t, node);
    list_del(&last_block->node);
    if (last_block->is_dynamic == TRUE) kfree(last_block);

    // 4. 🌟 从位图中彻底注销该 Chunk 占用的虚拟地址
    clear_chunk_bit(chunk->start_off / PCPU_CHUNK_SIZE);

    // 5. 摘除并销毁 Chunk 结构体
    list_del(&chunk->node);
    kfree(chunk);
}


void *alloc_percpu(uint32 size, uint32 align) {
    if (size == 0) return NULL;

    spin_lock(&pcpu_alloc_lock);

retry:
    list_head_t *chunk_pos;
    list_for_each(chunk_pos, &chunk_list) {
        pcpu_chunk_t *chunk = CONTAINER_OF(chunk_pos, pcpu_chunk_t, node);
        if (chunk->free_bytes < size) continue;

        list_head_t *blk_pos;
        list_for_each(blk_pos, &chunk->block_list) {
            pcpu_block_t *block = CONTAINER_OF(blk_pos, pcpu_block_t, node);

            if (!block->is_free) continue;

            uint32 aligned_offset = (block->offset + align - 1) & ~(align - 1);
            uint32 align_padding  = aligned_offset - block->offset;
            uint32 needed_size    = size + align_padding;

            if (block->size >= needed_size) {
                uint32 remaining_size = block->size - needed_size;

                if (remaining_size > 0) {
                    pcpu_block_t *split_blk = kmalloc(sizeof(pcpu_block_t));
                    split_blk->offset     = aligned_offset + size;
                    split_blk->size       = remaining_size;
                    split_blk->is_free    = TRUE;
                    split_blk->is_dynamic = TRUE;
                    list_add(&split_blk->node, &block->node);
                }

                block->size    = needed_size;
                block->is_free = FALSE;
                chunk->free_bytes -= needed_size;

                spin_unlock(&pcpu_alloc_lock);
                return (void *)(uint64)aligned_offset;
            }
        }
    }

    pcpu_chunk_t *new_chunk = pcpu_expand_chunk();
    if (new_chunk) goto retry;

    spin_unlock(&pcpu_alloc_lock);
    return NULL;
}


void free_percpu(void *ptr) {
    if (!ptr) return;

    uint32 target_offset = (uint32)(uint64)ptr;
    spin_lock(&pcpu_alloc_lock);

    list_head_t *chunk_pos;
    list_for_each(chunk_pos, &chunk_list) {
        pcpu_chunk_t *chunk = CONTAINER_OF(chunk_pos, pcpu_chunk_t, node);

        if (target_offset >= chunk->start_off && target_offset < chunk->end_off) {
            list_head_t *blk_pos;
            list_for_each(blk_pos, &chunk->block_list) {
                pcpu_block_t *block = CONTAINER_OF(blk_pos, pcpu_block_t, node);

                if (block->offset <= target_offset && (block->offset + block->size) > target_offset) {
                    if (block->is_free) {
                        PR_WARN("Double free in percpu!");
                    }

                    block->is_free = TRUE;
                    chunk->free_bytes += block->size;

                    if (block->node.next != &chunk->block_list) {
                        pcpu_block_t *next_blk = CONTAINER_OF(block->node.next, pcpu_block_t, node);
                        if (next_blk->is_free) {
                            block->size += next_blk->size;
                            list_del(&next_blk->node);
                            if (next_blk->is_dynamic == TRUE) kfree(next_blk);
                        }
                    }

                    if (block->node.prev != &chunk->block_list) {
                        pcpu_block_t *prev_blk = CONTAINER_OF(block->node.prev, pcpu_block_t, node);
                        if (prev_blk->is_free) {
                            prev_blk->size += block->size;
                            list_del(&block->node);
                            if (block->is_dynamic == TRUE) kfree(block);
                        }
                    }

                    // 🌟 终极清理
                    pcpu_recycle_empty_chunk(chunk);

                    spin_unlock(&pcpu_alloc_lock);
                    return;
                }
            }
        }
    }
    spin_unlock(&pcpu_alloc_lock);
    PR_WARN("free_percpu: Invalid pointer 0x%x!", target_offset);
}