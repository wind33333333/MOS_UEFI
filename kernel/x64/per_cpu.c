#include "per_cpu.h"
#include "cpu.h"
#include "../init/apic_init.h"
#include "vmm.h"
#include "pmm.h"
#include "printk.h"
#include "slub.h"

extern cpu_apic_info_t cpu_apic_info;
extern vm_space_t kernel_space;

static list_head_t chunk_list;
static uint32      pcpu_alloc_lock;
static uint32      global_max_offset = 0; // 记录当前系统扩张到的最大偏移量

static pcpu_chunk_t first_chunk;       // 第一个大块手动分配
static pcpu_block_t pcpu_cpu_block;    // 第一个小块手动分配，存放per_cpu结构
static pcpu_block_t free_block;        // 第一个手动拆下来的空闲块。

void per_cpu_mem_init() {
    uint32 total_cpu_count = cpu_apic_info.total_cpu_count;
    cpu_info.total_cpu_count = total_cpu_count;
    uint64 per_cpu_va = vm_layout.percpu_vaddr_start;

    for (uint32 i = 0; i < total_cpu_count; i++) {
        uint64 pa = page_to_pa(alloc_pages(4));
        vm_map_range(&kernel_space, per_cpu_va, pa, PCPU_CHUNK_SIZE, PAGE_KERNEL_DATA_RW);
        per_cpu_va += PERCPU_WINDOW_SIZE;
    }

    //初始化chunk头
    list_head_init(&chunk_list);
    list_head_init(&first_chunk.block_list);
    global_max_offset = PCPU_CHUNK_SIZE;

    //手动创建第一个chunk
    first_chunk.start_off = 0;
    first_chunk.end_off = PCPU_CHUNK_SIZE;
    first_chunk.free_bytes = PCPU_CHUNK_SIZE - sizeof(per_cpu_t); // 修复换行截断
    list_add_tail( &first_chunk.node,&chunk_list);

    //手动创建per_cpu的block
    pcpu_cpu_block.is_free = FALSE;
    pcpu_cpu_block.is_dynamic = FALSE;
    pcpu_cpu_block.offset = 0;
    pcpu_cpu_block.size = sizeof(per_cpu_t);
    list_add_tail( &pcpu_cpu_block.node,&first_chunk.block_list);

    //手动创建第一个空闲block
    free_block.is_free = TRUE;
    free_block.is_dynamic = FALSE;
    free_block.offset = sizeof(per_cpu_t);
    free_block.size = PCPU_CHUNK_SIZE - sizeof(per_cpu_t);
    list_add_tail( &free_block.node,&first_chunk.block_list);
}


// 🌟 扩容函数：创造一个新的 Chunk 并映射给所有 CPU
static inline pcpu_chunk_t *pcpu_expand_chunk(void) {
    // 1. 创建全局 Chunk 控制块
    pcpu_chunk_t *chunk = kmalloc(sizeof(pcpu_chunk_t));
    if (!chunk) return NULL;

    chunk->start_off  = global_max_offset;
    chunk->end_off    = global_max_offset + PCPU_CHUNK_SIZE;
    chunk->free_bytes = PCPU_CHUNK_SIZE;
    list_head_init(&chunk->block_list);

    uint64 per_cpu_va = vm_layout.percpu_vaddr_start + global_max_offset;

    // 2. 为系统中的【每一个】CPU 分配真实的物理页，并映射到连续的虚拟空间！
    for (uint32 i = 0; i < cpu_info.total_cpu_count; i++) {
        uint64 pa = page_to_pa(alloc_pages(4));
        vm_map_range(&kernel_space, per_cpu_va, pa, PCPU_CHUNK_SIZE, PAGE_KERNEL_DATA_RW);
        per_cpu_va += PERCPU_WINDOW_SIZE;
    }

    // 3. 创建一个涵盖整个新 Chunk 的初始空闲大块
    pcpu_block_t *init_block = kmalloc(sizeof(pcpu_block_t));
    init_block->offset  = chunk->start_off;
    init_block->size    = PCPU_CHUNK_SIZE;
    init_block->is_free = TRUE;
    init_block->is_dynamic = TRUE; // 🌟 新增：标记为动态块
    list_add_tail( &init_block->node,&chunk->block_list);

    // 4. 更新全局状态
    global_max_offset += PCPU_CHUNK_SIZE;
    list_add_tail( &chunk->node,&chunk_list);

    return chunk;
}

// =========================================================================
// 🌟 进阶：空闲 Chunk 终极回收引擎 (内联封装)
// =========================================================================
static inline void pcpu_recycle_empty_chunk(pcpu_chunk_t *chunk) {

    // 1. 阈值与防御性拦截：必须全空，且绝对不能是创世 Chunk
    if (chunk->free_bytes != PCPU_CHUNK_SIZE || chunk == &first_chunk) {
        return;
    }

    // 2. 释放底层物理页，并解除 VMM 映射
    for (uint32 i = 0; i < cpu_info.total_cpu_count; i++) {
        uint64 per_cpu_va = vm_layout.percpu_vaddr_start + chunk->start_off + (i * PERCPU_WINDOW_SIZE);

        // 解除页表映射 (清理 PTE 等级)
        vm_unmap_range(&kernel_space, per_cpu_va, PCPU_CHUNK_SIZE,UNMAP_FLAG_FREE_PHYS);
    }

    // 3. 销毁最后残留的那个涵盖整个 Chunk 的空闲 Block 元数据
    pcpu_block_t *last_block = CONTAINER_OF(chunk->block_list.next, pcpu_block_t, node);
    list_del(&last_block->node);
    if (last_block->is_dynamic == TRUE) {
        kfree(last_block);
    }

    // 4. 从全局 Chunk 链表摘除，并将 Chunk 自身送进焚尸炉
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

            // 必须是空闲块
            if (!block->is_free) continue;

            // 🌟 修复：完整的位运算语法
            uint32 aligned_offset = (block->offset + align - 1) & ~(align - 1);
            uint32 align_padding  = aligned_offset - block->offset;
            uint32 needed_size    = size + align_padding;

            if (block->size >= needed_size) {
                // 命中！执行块分裂 (Split)
                uint32 remaining_size = block->size - needed_size;

                if (remaining_size > 0) {
                    // 产生剩余空间，创建一个新的元数据块来管理它
                    pcpu_block_t *split_blk = kmalloc(sizeof(pcpu_block_t));
                    split_blk->offset  = aligned_offset + size;
                    split_blk->size    = remaining_size;
                    split_blk->is_free = TRUE;
                    split_blk->is_dynamic = TRUE; // 🌟 新增：分裂出来的新块必然是动态的

                    // 🌟 修复：将其紧密插入到当前 block 的正后方
                    // 注意：标准链表 API 中，list_add 才是插入到指定节点后面，如果使用 list_add_tail 会被插到链表末尾
                    list_add_tail(&split_blk->node,&block->node );
                }

                // 更新当前块为已占用
                block->size    = needed_size;
                block->is_free = FALSE;
                chunk->free_bytes -= needed_size;

                spin_unlock(&pcpu_alloc_lock);
                // 返回带有对齐偏移的真实 offset
                return (void *)(uint64)aligned_offset;
            }
        }
    }

    // 所有的 Chunk 都没有足够的空间，触发动态扩容！
    pcpu_chunk_t *new_chunk = pcpu_expand_chunk();
    if (new_chunk) {
        goto retry; // 扩容成功，重新走一遍分配流程
    }

    spin_unlock(&pcpu_alloc_lock);
    return NULL; // 彻底 OOM
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

                    // 1. 标记为空闲
                    block->is_free = TRUE;
                    chunk->free_bytes += block->size;

                    // 2. 向后融合 (Merge Next)
                    if (block->node.next != &chunk->block_list) {
                        pcpu_block_t *next_blk = CONTAINER_OF(block->node.next, pcpu_block_t, node);
                        if (next_blk->is_free) {
                            block->size += next_blk->size;
                            list_del(&next_blk->node);
                            if (next_blk->is_dynamic == TRUE) {
                                kfree(next_blk);
                            }
                        }
                    }

                    // 3. 向前融合 (Merge Prev)
                    if (block->node.prev != &chunk->block_list) {
                        pcpu_block_t *prev_blk = CONTAINER_OF(block->node.prev, pcpu_block_t, node);
                        if (prev_blk->is_free) {
                            prev_blk->size += block->size;
                            list_del(&block->node);
                            if (block->is_dynamic == TRUE) {
                                kfree(block);
                            }
                        }
                    }

                    // 🌟 4. 终极清理：尝试回收全空的 Chunk (内联调用)
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