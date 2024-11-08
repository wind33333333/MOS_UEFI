#include "memory.h"
#include "printk.h"
#include "uefi.h"
#include "page.h"

global_memory_descriptor_t memory_management;

UINT64 *pml4t_vbase;  //pml4t虚拟地址基址
UINT64 *pdptt_vbase;  //pdptt虚拟地址基址
UINT64 *pdt_vbase;    //pdt虚拟地址基址
UINT64 *ptt_vbase;    //ptt虚拟地址基址

__attribute__((section(".init_text"))) void init_memory(void) {
    pml4t_vbase = (UINT64 *) 0xFFFFFFFFFFFFF000;  //pml4t虚拟地址基址
    pdptt_vbase = (UINT64 *) 0xFFFFFFFFFFE00000;  //pdptt虚拟地址基址
    pdt_vbase = (UINT64 *) 0xFFFFFFFFC0000000;    //pdt虚拟地址基址
    ptt_vbase= (UINT64 *) 0xFFFFFF8000000000;    //ptt虚拟地址基址
    //初始化全局变量memory_management
    mem_set(&memory_management,0,sizeof(global_memory_descriptor_t));
    //查找memmap中可用物理内存并合并，统计总物理内存容量。
    UINT32 mem_map_index = 0;
    for(UINT32 i = 0;i < (boot_info->mem_map_size/boot_info->mem_descriptor_size);i++){
        // 使用逻辑或 (||) 来判断内存类型
        if(boot_info->mem_map[i].Type==EFI_LOADER_CODE || boot_info->mem_map[i].Type==EFI_LOADER_DATA || boot_info->mem_map[i].Type==EFI_BOOT_SERVICES_CODE || boot_info->mem_map[i].Type==EFI_BOOT_SERVICES_DATA || boot_info->mem_map[i].Type==EFI_CONVENTIONAL_MEMORY || boot_info->mem_map[i].Type==EFI_ACPI_RECLAIM_MEMORY){
            if(boot_info->mem_map[i].PhysicalStart==(memory_management.mem_map[mem_map_index].address+memory_management.mem_map[mem_map_index].length)){
                // 合并相邻的内存块
                memory_management.mem_map[mem_map_index].length+=(boot_info->mem_map[i].NumberOfPages<<12);
            }else if(memory_management.mem_map[mem_map_index].length!=0){
                // 开始记录新的内存块
                mem_map_index++;
                memory_management.mem_map[mem_map_index].address=boot_info->mem_map[i].PhysicalStart;
                memory_management.mem_map[mem_map_index].length=boot_info->mem_map[i].NumberOfPages<<12;
            }else{
                // 初始化新的内存块
                memory_management.mem_map[mem_map_index].address=boot_info->mem_map[i].PhysicalStart;
                memory_management.mem_map[mem_map_index].length=boot_info->mem_map[i].NumberOfPages<<12;
            }
            // 更新可用内存大小
            memory_management.avl_mem_size+=(boot_info->mem_map[i].NumberOfPages<<12);
            memory_management.mem_map[mem_map_index].type=1;
        }
    }
    // 最终更新 mem_map 的数量
    memory_management.mem_map_number=mem_map_index+1;
    //打印可用物理内存和总物理内存
    for(UINT32 i=0;i<memory_management.mem_map_number;i++) {
        color_printk(ORANGE, BLACK, "%d  Type:%d    Addr:%#018lX    Length:%#018lX\n",i,memory_management.mem_map[i].type,memory_management.mem_map[i].address, memory_management.mem_map[i].length);
    }
    color_printk(ORANGE, BLACK, "Available RAM:%#lX~%ldMB\n", memory_management.avl_mem_size,memory_management.avl_mem_size/1024/1024);

    // 全部置位 bitmap（置为1表示已使用，清除0表示未使用）
    memory_management.bitmap = (UINT64 *) kernel_stack_top;  //bimap的起始地址是kernel结束地址+0x4000
    memory_management.bitmap_size = (memory_management.mem_map[memory_management.mem_map_number - 1].address+memory_management.mem_map[memory_management.mem_map_number - 1].length) >> PAGE_4K_SHIFT;
    memory_management.bitmap_length =memory_management.bitmap_size>>3;
    mem_set(memory_management.bitmap, 0xff, memory_management.bitmap_length);

    //初始化bitmap，i=1跳过1M,1M之前的内存需要用来存放ap核初始化代码暂时保持置位，把后面可用的内存全部初始化，等全部初始化后再释放前1M
    for (UINT32 i = 1; i < memory_management.mem_map_number; i++) {
        free_pages((void*)memory_management.mem_map[i].address,
                   memory_management.mem_map[i].length >> PAGE_4K_SHIFT);
    }

    //kernel_end_address结束地址加上bit map对齐4K边界
    memory_management.kernel_start_address = (UINT64)_start_text;
    memory_management.kernel_end_address = PAGE_4K_ALIGN((UINT64)memory_management.bitmap + memory_management.bitmap_length);

    //通过free_pages函数的异或位操作实现把内核bitmap位图标记为已使用空间。
    free_pages((void*)HADDR_TO_LADDR(&_start_init_text), memory_management.kernel_end_address-(UINT64)_start_init_text>>PAGE_4K_SHIFT);

    //总物理页
    memory_management.total_pages=memory_management.avl_mem_size>>PAGE_4K_SHIFT;
    //已分配物理页
    memory_management.used_pages = (memory_management.mem_map[0].length+(memory_management.kernel_end_address-(UINT64)_start_init_text) >> PAGE_4K_SHIFT);
    //空闲物理页
    memory_management.avl_pages = memory_management.total_pages - memory_management.used_pages;

    color_printk(ORANGE, BLACK,
                 "Bitmap:%#lX \tBitmapSize:%#lX \tBitmapLength:%#lX\n",
                 memory_management.bitmap, memory_management.bitmap_size,
                 memory_management.bitmap_length);
    color_printk(ORANGE, BLACK, "Total 4K PAGEs:%ld \tAlloc:%ld \tFree:%ld\n",
                 memory_management.total_pages, memory_management.used_pages,
                 memory_management.avl_pages);
    color_printk(ORANGE, BLACK, "Init Kernel Start Addr:%#lX Official kernel Start Addr:%lX kernel end addr:%#lX\n",
                 _start_init_text,memory_management.kernel_start_address,memory_management.kernel_end_address);
    return;
}


//物理页分配器
void *alloc_pages(UINT64 page_number) {
    SPIN_LOCK(memory_management.lock);

    if (memory_management.avl_pages < page_number || page_number == 0) {
        memory_management.lock = 0;
        return (void *) -1;
    }

    UINT64 start_idx = 0;
    UINT64 current_length = 0;

    for (UINT64 i = 0; i < (memory_management.bitmap_size / 64); i++) {
        if ((memory_management.bitmap[i] == 0xFFFFFFFFFFFFFFFFUL)) {
            current_length = 0;
            continue;
        }

        for (UINT64 j = 0; j < 64; j++) {
            if (memory_management.bitmap[i] & (1UL << j)) {
                current_length = 0;
                continue;
            }

            if (current_length == 0)
                start_idx = i * 64 + j;

            current_length++;

            if (current_length == page_number) {
                for (UINT64 y = 0; y < page_number; y++) {
                    (memory_management.bitmap[(start_idx + y) / 64] |= (1UL << ((start_idx + y) % 64)));
                }

                memory_management.used_pages += page_number;
                memory_management.avl_pages -= page_number;
                memory_management.lock = 0;
                return (void *) (start_idx << PAGE_4K_SHIFT); // 找到连续空闲块，返回起始索引
            }
        }
    }

    memory_management.lock = 0;
    return (void *) -1; // 没有找到足够大的连续内存块
}


//物理页释放器
int free_pages(void *pages_addr, UINT64 page_number) {
    SPIN_LOCK(memory_management.lock);
    if ((UINT64)(pages_addr + (page_number << PAGE_4K_SHIFT)) >
        (memory_management.mem_map[memory_management.mem_map_number - 1].address +
         memory_management.mem_map[memory_management.mem_map_number - 1].length)) {
        memory_management.lock = 0;
        return -1;
    }

    for (UINT64 i = 0; i < page_number; i++) {
        (memory_management.bitmap[(((UINT64) pages_addr >> PAGE_4K_SHIFT) + i) / 64] ^= (1UL<< (((UINT64) pages_addr >> PAGE_4K_SHIFT) + i) % 64));
    }
    memory_management.used_pages -= page_number;
    memory_management.avl_pages += page_number;
    memory_management.lock = 0;
    return 0;
}


//释放物理内存映射虚拟内存
void unmap_pages(UINT64 vir_addr, UINT64 page_number) {
    UINT64 nums[] = {
            ((page_number + ((vir_addr >> 12) - ((vir_addr >> 12) & ~(512UL - 1)))) + (512UL - 1)) / 512UL,
            ((page_number + ((vir_addr >> 12) - ((vir_addr >> 12) & ~(512UL * 512 - 1)))) + (512UL * 512 - 1)) / (512UL * 512),
            ((page_number + ((vir_addr >> 12) - ((vir_addr >> 12) & ~(512UL * 512 * 512 - 1)))) + (512UL * 512 * 512 - 1)) / (512UL * 512 * 512)};
    UINT64 level_offsets[] = {21, 30, 39};
    UINT64 *table_bases[] = {ptt_vbase, pdt_vbase, pdptt_vbase, pml4t_vbase};
    UINT64 offset = vir_addr & 0xFFFFFFFFFFFFUL;
    UINT64 k;

    //释放页表 PT
    free_pages((void *) (ptt_vbase[(offset >> 12)] & PAGE_4K_MASK), page_number);
    mem_set(&ptt_vbase[(offset >> 12)], 0, page_number << 3);

    // 刷新 TLB
    for (UINT64 i = 0; i < page_number; i++) {
        INVLPG((vir_addr & PAGE_4K_MASK) + i * 4096);
    }

    //释放页目录表PD 页目录指针表PDPT 四级页目录表PLM4
    for (UINT64 i = 0; i < 3; i++) {
        for (UINT64 j = 0; j < nums[i]; j++) {
            k = 0;
            while (table_bases[i][(offset >> level_offsets[i] << 9) + j * 512 + k] == 0) {
                if (k == 511) {
                    free_pages((void *) ((table_bases[i + 1][(offset >> level_offsets[i]) + j]) & PAGE_4K_MASK), 1);
                    table_bases[i + 1][(offset >> level_offsets[i]) + j] = 0;
                    break;
                }
                k++;
            }
        }
    }

    return;
}


//物理内存映射虚拟内存
void map_pages(UINT64 paddr, UINT64 vir_addr, UINT64 page_number, UINT64 attr) {

    UINT64 nums[] = {
            ((page_number + ((vir_addr >> 12) - ((vir_addr >> 12) & ~(512UL * 512 * 512 - 1)))) + (512UL * 512 * 512 - 1)) / (512UL * 512 * 512),
            ((page_number + ((vir_addr >> 12) - ((vir_addr >> 12) & ~(512UL * 512 - 1)))) + (512UL * 512 - 1)) / (512UL * 512),
            ((page_number + ((vir_addr >> 12) - ((vir_addr >> 12) & ~(512UL - 1)))) + (512UL - 1)) / 512UL};
    UINT64 level_offsets[] = {39, 30, 21};
    UINT64 *table_bases[] = {pml4t_vbase, pdptt_vbase, pdt_vbase, ptt_vbase};
    UINT64 offset = vir_addr & 0xFFFFFFFFFFFFUL;

    for (UINT64 i = 0; i < 3; i++) {
        for (UINT64 j = 0; j < nums[i]; j++) {
            if (table_bases[i][(offset >> level_offsets[i]) + j] == 0) {
                table_bases[i][(offset >> level_offsets[i]) + j] =
                        (UINT64) alloc_pages(1) | (attr & 0x3F | PAGE_RW);
                mem_set(&table_bases[i + 1][(offset >> level_offsets[i] << 9) + j * 512], 0x0, 4096);
            }
        }
    }

    //PT 映射页表
    for (UINT64 i = 0; i < page_number; i++) {
        ptt_vbase[(offset >> 12) + i] = (paddr & PAGE_4K_MASK) + i * 4096 | attr;
        INVLPG((vir_addr & PAGE_4K_MASK) + i * 4096);
    }

    return;
}


/*

//物理内存映射虚拟内存
void
map_pages11(UINT64 paddr, UINT64 vir_addr, UINT64 page_number, UINT64 attr) {

    UINT64 num;
    UINT64 offset = vir_addr & 0xFFFFFFFFFFFFUL;

    //PML4 映射四级页目录表
    num = ((page_number + ((vir_addr >> 12) - ((vir_addr >> 12) & ~(512UL * 512 * 512 - 1)))) +
           (512UL * 512 * 512 - 1)) / (512UL * 512 * 512);
    for (UINT64 i = 0; i < num; i++) {
        if (pml4t_vbase[(offset >> 39) + i] == 0) {
            pml4t_vbase[(offset >> 39) + i] =
                    (UINT64) alloc_pages(1) | (attr & 0x3F | PAGE_RW);
            mem_set(&pdptt_vbase[(offset >> 39 << 9) + i * 512], 0x0, 4096);
        }
    }

    //PDPT 映射页目录指针表
    num = ((page_number + ((vir_addr >> 12) - ((vir_addr >> 12) & ~(512UL * 512 - 1)))) +
           (512UL * 512 - 1)) /
          (512UL * 512);
    for (UINT64 i = 0; i < num; i++) {
        if (pdptt_vbase[(offset >> 30) + i] == 0) {
            pdptt_vbase[(offset >> 30) + i] =
                    (UINT64) alloc_pages(1) | (attr & 0x13F | PAGE_RW);
            mem_set(&pdt_vbase[(offset >> 30 << 9) + i * 512], 0x0, 4096);
        }
    }

    //PD 映射页目录表
    num = ((page_number + ((vir_addr >> 12) - ((vir_addr >> 12) & ~(512UL - 1)))) + (512UL - 1)) / 512UL;
    for (UINT64 i = 0; i < num; i++) {
        if (pdt_vbase[(offset >> 21) + i] == 0) {
            pdt_vbase[(offset >> 21) + i] =
                    (UINT64) alloc_pages(1) | (attr & 0x13F | PAGE_RW);
            mem_set(&ptt_vbase[(offset >> 21 << 9) + i * 512], 0x0, 4096);
        }
    }

    //PT 映射页表
    for (UINT64 i = 0; i < page_number; i++) {
        ptt_vbase[(offset >> 12) + i] = (paddr & PAGE_4K_MASK) + i * 4096 | attr;
        INVLPG((vir_addr & PAGE_4K_MASK) + i * 4096);
    }

    return;
}


//释放物理内存映射虚拟内存
void unmap_pages11(UINT64 vir_addr, UINT64 page_number) {
    UINT64 num, j;
    UINT64 offset = vir_addr & 0xFFFFFFFFFFFFUL;

    //释放页表 PT
    free_pages((void *) (ptt_vbase[(offset >> 12)] & PAGE_4K_MASK), page_number);
    mem_set(&ptt_vbase[(offset >> 12)], 0, page_number << 3);

    // 刷新 TLB
    for (UINT64 i = 0; i < page_number; i++) {
        INVLPG((vir_addr & PAGE_4K_MASK) + i * 4096);
    }

    //释放页目录 PD
    num = ((page_number + ((vir_addr >> 12) - ((vir_addr >> 12) & ~(512UL - 1)))) + (512UL - 1)) / 512UL;
    for (UINT64 i = 0; i < num; i++) {
        j = 0;
        while (ptt_vbase[(offset >> 21 << 9) + i * 512 + j] == 0) {
            if (j == 511) {
                free_pages((void *) (pdt_vbase[(offset >> 21) + i] & PAGE_4K_MASK), 1);
                pdt_vbase[(offset >> 21) + i] = 0;
                break;
            }
            j++;
        }
    }

    //释放页目录表 PDPT
    num = ((page_number + ((vir_addr >> 12) - ((vir_addr >> 12) & ~(512UL * 512 - 1)))) +
           (512UL * 512 - 1)) /
          (512UL * 512);
    for (UINT64 i = 0; i < num; i++) {
        j = 0;
        while (pdt_vbase[(offset >> 30 << 9) + i * 512UL + j] == 0) {
            if (j == 511) {
                free_pages((void *) (pdptt_vbase[(offset >> 30) + i] & PAGE_4K_MASK), 1);
                pdptt_vbase[(offset >> 30) + i] = 0;
                break;
            }
            j++;
        }
    }

    //释放页目录表 PML4T
    num = ((page_number + ((vir_addr >> 12) - ((vir_addr >> 12) & ~(512UL * 512 * 512 - 1)))) +
           (512UL * 512 * 512 - 1)) / (512UL * 512 * 512);
    for (UINT64 i = 0; i < num; i++) {
        j = 0;
        while (pdptt_vbase[(offset >> 39 << 9) + i * 512UL + j] == 0) {
            if (j == 511) {
                free_pages((void *) (pml4t_vbase[(offset >> 39) + i] & PAGE_4K_MASK), 1);
                pml4t_vbase[(offset >> 39) + i] = 0;
                break;
            }
            j++;
        }
    }

    return;
}

*/