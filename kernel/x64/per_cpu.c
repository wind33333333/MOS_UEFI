#include "per_cpu.h"
#include "cpu.h"
#include "../init/apic_init.h"
#include "vmm.h"
#include "pmm.h"
#include "printk.h"
#include "slub.h"

extern cpu_apic_info_t cpu_apic_info;
extern vm_space_t kernel_space;

list_head_t pcpu_chunk_head;
uint32  pcpu_alloc_lock;
uint32      global_max_offset = 0; // 记录当前系统扩张到的最大偏移量

pcpu_chunk_t pcpu_chunk;    //第一个大块手动分配
pcpu_block_t pcpu_block;    //第一个小块手动分配，存放per_cpu结构
pcpu_block_t free_block;    //第一个手动拆下来的空闲块。

void per_cpu_mem_init() {
    uint32 total_cpu_count = cpu_apic_info.total_cpu_count;
    uint64 per_cpu_va = vm_layout.percpu_vaddr_start;
    for (uint32 i = 0; i < total_cpu_count; i++) {
        uint64 pa = page_to_pa(alloc_pages(4));
        vm_map_range(&kernel_space,per_cpu_va,pa,PCPU_CHUNK_SIZE,PAGE_KERNEL_DATA_RW);
        per_cpu_va += PERCPU_WINDOW_SIZE;
    }

    list_head_init(&pcpu_chunk_head);
    list_head_init(&pcpu_chunk.block_list);
    global_max_offset = PCPU_CHUNK_SIZE;

    pcpu_chunk.start_off = 0;
    pcpu_chunk.end_off = PCPU_CHUNK_SIZE;
    pcpu_chunk.free_bytes = PCPU_CHUNK_SIZE - sizeof(per_cpu_t);
    list_add_tail(&pcpu_chunk_head,&pcpu_chunk.list);

    pcpu_block.is_free = FALSE;
    pcpu_block.offset = 0;
    pcpu_block.size = sizeof(per_cpu_t);
    list_add_tail(&pcpu_chunk.block_list,&pcpu_block.list);

    free_block.is_free = TRUE;
    free_block.offset = sizeof(per_cpu_t);
    free_block.size = PCPU_CHUNK_SIZE -sizeof(per_cpu_t);
    list_add_tail(&pcpu_chunk.block_list,&free_block.list);

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

    uint64 per_cpu_va = vm_layout.percpu_vaddr_start+global_max_offset;

    // 2. 为系统中的【每一个】CPU 分配真实的物理页，并映射到连续的虚拟空间！
    for (uint32 i = 0; i < cpu_info.total_cpu_count; i++) { //[cite: 9]
        uint64 pa = page_to_pa(alloc_pages(4));
        // 核心魔法：将零散的物理页，连续映射到该 CPU 的专属虚拟窗口中
        vm_map_range(&kernel_space,per_cpu_va,pa,PCPU_CHUNK_SIZE,PAGE_KERNEL_DATA_RW);
        per_cpu_va += PERCPU_WINDOW_SIZE;
    }

    // 3. 创建一个涵盖整个新 Chunk 的初始空闲大块
    pcpu_block_t *init_block = kmalloc(sizeof(pcpu_block_t));
    init_block->offset  = chunk->start_off;
    init_block->size    = PCPU_CHUNK_SIZE;
    init_block->is_free = TRUE;
    list_add_tail(&chunk->block_list, &init_block->list);

    // 4. 更新全局状态
    global_max_offset += PCPU_CHUNK_SIZE;
    list_add_tail(&pcpu_chunk_head, &chunk->list);

    return chunk;
}


void *alloc_percpu(uint32 size, uint32 align) {
    if (size == 0) return NULL;

    spin_lock(&pcpu_alloc_lock);

    retry:
        list_head_t *chunk_pos;
    list_for_each(chunk_pos, &pcpu_chunk_head) {
        pcpu_chunk_t *chunk = CONTAINER_OF(chunk_pos, pcpu_chunk_t, list);
        if (chunk->free_bytes < size) continue;

        list_head_t *blk_pos;
        list_for_each(blk_pos, &chunk->block_list) {
            pcpu_block_t *block = CONTAINER_OF(blk_pos, pcpu_block_t, list);

            // 必须是空闲块
            if (!block->is_free) continue;

            // 计算对齐后的偏移量 (补齐开销)
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

                    // 插入到当前块的后面
                    list_add_tail(&block->list, &split_blk->list);
                }

                // 更新当前块为已占用
                block->size    = needed_size;
                block->is_free = FALSE;
                chunk->free_bytes -= needed_size;

                spin_unlock(&pcpu_alloc_lock);
                // 🌟 返回带有对齐偏移的真实 offset
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
    list_for_each(chunk_pos, &pcpu_chunk_head) {
        pcpu_chunk_t *chunk = CONTAINER_OF(chunk_pos, pcpu_chunk_t, list);

        // 范围判定：确定这个 offset 属于哪个 Chunk
        if (target_offset >= chunk->start_off && target_offset < chunk->end_off) {

            list_head_t *blk_pos;
            list_for_each(blk_pos, &chunk->block_list) {
                pcpu_block_t *block = CONTAINER_OF(blk_pos, pcpu_block_t, list);

                // 考虑到分配时做了对齐(align_padding)，查找条件需要稍微放宽
                if (block->offset <= target_offset && (block->offset + block->size) > target_offset) {

                    if (block->is_free) {
                        PR_WARN("Double free in percpu!");
                    }

                    // 1. 标记为空闲
                    block->is_free = TRUE;
                    chunk->free_bytes += block->size;

                    // 2. 向后融合 (Merge Next)
                    if (block->list.next != &chunk->block_list) {
                        pcpu_block_t *next_blk = CONTAINER_OF(block->list.next, pcpu_block_t, list);
                        if (next_blk->is_free) {
                            block->size += next_blk->size;
                            list_del(&next_blk->list);
                            kfree(next_blk); // 销毁多余的元数据块[cite: 6]
                        }
                    }

                    // 3. 向前融合 (Merge Prev)
                    if (block->list.prev != &chunk->block_list) {
                        pcpu_block_t *prev_blk = CONTAINER_OF(block->list.prev, pcpu_block_t, list);
                        if (prev_blk->is_free) {
                            prev_blk->size += block->size;
                            prev_blk->offset -= block->size;
                            list_del(&block->list);
                            kfree(block); // 销毁多余的元数据块[cite: 6]
                        }
                    }

                    // (进阶可选): 如果 chunk->free_bytes == PCPU_CHUNK_SIZE，
                    // 说明这个 Chunk 彻底空了，可以解除映射并将物理页还给 pmm。

                    spin_unlock(&pcpu_alloc_lock);
                    return;
                }
            }
        }
    }
    spin_unlock(&pcpu_alloc_lock);
    PR_OK("free_percpu: Invalid pointer!");
}