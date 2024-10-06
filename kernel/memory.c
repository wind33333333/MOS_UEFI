#include "memory.h"
#include "printk.h"

__attribute__((section(".init_text"))) void memoryInit(unsigned char bspFlags) {

    if (bspFlags) {
        unsigned int x = 0;
        unsigned long totalmem = 0;
        struct E820 *p = (struct E820 *) E820_BASE;
        for (unsigned int i = 0; i < *(unsigned int *) E820_SIZE; i++) {
            color_printk(ORANGE, BLACK, "Addr: %#018lX\t Len: %#018lX\t Type: %d\n", p->address,p->length, p->type);
            if (p->type == 1) {
                memory_management_struct.e820[x].address = p->address & PAGE_4K_MASK;
                memory_management_struct.e820[x].length = p->length & PAGE_4K_MASK;
                memory_management_struct.e820[x].type = p->type;
                memory_management_struct.e820_length++;
                totalmem += p->length;
                memory_management_struct.total_pages +=
                        memory_management_struct.e820[x].length >> PAGE_4K_SHIFT;
                x++;
            }
            p++;
        }
        color_printk(ORANGE, BLACK, "OS Can Used Total RAM: %#018lX=%ldMB\n", totalmem,
                     totalmem / 1024 / 1024);

        //bits map construction init
        totalmem = memory_management_struct.e820[memory_management_struct.e820_length - 1].address +
                   memory_management_struct.e820[memory_management_struct.e820_length - 1].length;
        memory_management_struct.bits_map = (unsigned long *) kenelstack_top;
        memory_management_struct.bits_size = totalmem >> PAGE_4K_SHIFT;
        memory_management_struct.bits_length =
                (memory_management_struct.bits_size + 63) / 8 & 0xFFFFFFFFFFFFFFF8UL;
        memset(memory_management_struct.bits_map, 0xff, memory_management_struct.bits_length);

        //bit map 1M以上可用空间置0，i=1跳过1M保持使用置1，等全部初始化后再释放
        for (unsigned int i = 1; i < memory_management_struct.e820_length; i++) {
            memset(memory_management_struct.bits_map +
                   ((memory_management_struct.e820[i].address >> PAGE_4K_SHIFT) >> 6), 0,
                   (memory_management_struct.e820[i].length >> PAGE_4K_SHIFT) >> 3);
            totalmem = memory_management_struct.e820[i].address +
                       memory_management_struct.e820[i].length & 0xFFFFFFFFFFFF8000UL;
            for (; totalmem < (memory_management_struct.e820[i].address +
                               memory_management_struct.e820[i].length); totalmem += PAGE_4K_SIZE) {
                *(memory_management_struct.bits_map + (totalmem >> PAGE_4K_SHIFT >> 6)) ^=
                        1UL << (totalmem >> PAGE_4K_SHIFT) % 64;
            }
        }

        //kernel_end结束地址加上bit map对齐4K地址
        memory_management_struct.kernel_start = (unsigned long) &_start_text;
        memory_management_struct.kernel_end =
                kenelstack_top + (memory_management_struct.bits_length + 0xfff) &
                0xFFFFFFFFFFFFF000UL;

        //把内核1M开始到kernel_end地址bit map置1，标记为已使用
        memset(memory_management_struct.bits_map + ((0x100000 >> PAGE_4K_SHIFT) >> 6), 0xFF,
               (HADDR_TO_LADDR(memory_management_struct.kernel_end) - 0x100000) >> PAGE_4K_SHIFT
                                                                                >> 3);
        totalmem = HADDR_TO_LADDR(memory_management_struct.kernel_end) & 0xFFFFFFFFFFFF8000UL;
        for (; totalmem <
               HADDR_TO_LADDR(memory_management_struct.kernel_end); totalmem += PAGE_4K_SIZE) {
            *(memory_management_struct.bits_map + (totalmem >> PAGE_4K_SHIFT >> 6)) ^=
                    1UL << ((totalmem - 0x100000) >> PAGE_4K_SHIFT) % 64;
        }

        memory_management_struct.alloc_pages += (memory_management_struct.e820[0].length
                >> PAGE_4K_SHIFT);
        memory_management_struct.alloc_pages += (
                (HADDR_TO_LADDR(memory_management_struct.kernel_end) - 0x100000) >> PAGE_4K_SHIFT);
        memory_management_struct.free_pages =
                memory_management_struct.total_pages - memory_management_struct.alloc_pages;

        color_printk(ORANGE, BLACK,
                     "bits_map: %#018lX \tbits_size: %#018lX \tbits_length: %#018lX\n",
                     memory_management_struct.bits_map, memory_management_struct.bits_size,
                     memory_management_struct.bits_length);
        color_printk(ORANGE, BLACK, "OS Can Used Total 4K PAGEs: %ld \tAlloc: %ld \tFree: %ld\n",
                     memory_management_struct.total_pages, memory_management_struct.alloc_pages,
                     memory_management_struct.free_pages);
    }
    return;
}


////物理页分配器
void *alloc_pages(unsigned long page_num) {
    SPIN_LOCK(memory_management_struct.lock);

    if (memory_management_struct.free_pages < page_num || page_num == 0) {
        memory_management_struct.lock = 0;
        return (void *) -1;
    }

    unsigned long start_idx = 0;
    unsigned long current_length = 0;

    for (unsigned long i = 0; i < (memory_management_struct.bits_size / 64); i++) {
        if ((memory_management_struct.bits_map[i] == 0xFFFFFFFFFFFFFFFFUL)) {
            current_length = 0;
            continue;
        }

        for (unsigned long j = 0; j < 64; j++) {
            if (memory_management_struct.bits_map[i] & (1UL << j)) {
                current_length = 0;
                continue;
            }

            if (current_length == 0)
                start_idx = i * 64 + j;

            current_length++;

            if (current_length == page_num) {
                for (unsigned long y = 0; y < page_num; y++) {
                    (memory_management_struct.bits_map[(start_idx + y) / 64] |= (1UL
                            << ((start_idx + y) % 64)));
                }

                memory_management_struct.alloc_pages += page_num;
                memory_management_struct.free_pages -= page_num;
                memory_management_struct.lock = 0;
                return (void *) (start_idx << PAGE_4K_SHIFT); // 找到连续空闲块，返回起始索引
            }
        }
    }

    memory_management_struct.lock = 0;
    return (void *) -1; // 没有找到足够大的连续内存块
}


///物理页释放器
int free_pages(void *pages_addr, unsigned long page_num) {
    SPIN_LOCK(memory_management_struct.lock);
    if ((pages_addr + (page_num << PAGE_4K_SHIFT)) >
        (memory_management_struct.e820[memory_management_struct.e820_length - 1].address +
         memory_management_struct.e820[memory_management_struct.e820_length - 1].length)) {
        memory_management_struct.lock = 0;
        return -1;
    }

    for (unsigned long i = 0; i < page_num; i++) {
        (memory_management_struct.bits_map[(((unsigned long) pages_addr >> PAGE_4K_SHIFT) + i) /
                                           64] ^= (1UL
                << (((unsigned long) pages_addr >> PAGE_4K_SHIFT) + i) % 64));
    }
    memory_management_struct.alloc_pages -= page_num;
    memory_management_struct.free_pages += page_num;
    memory_management_struct.lock = 0;
    return 0;
}


//释放物理内存映射虚拟内存
void unmap_pages(unsigned long vaddr, unsigned long page_num) {
    unsigned long nums[] = {
            ((page_num + ((vaddr >> 12) - ((vaddr >> 12) & ~(512UL - 1)))) + (512UL - 1)) / 512UL,
            ((page_num + ((vaddr >> 12) - ((vaddr >> 12) & ~(512UL * 512 - 1)))) +
             (512UL * 512 - 1)) / (512UL * 512),
            ((page_num + ((vaddr >> 12) - ((vaddr >> 12) & ~(512UL * 512 * 512 - 1)))) +
             (512UL * 512 * 512 - 1)) / (512UL * 512 * 512)};
    unsigned long level_offsets[] = {21, 30, 39};
    unsigned long *table_bases[] = {ptt_vbase, pdt_vbase, pdptt_vbase, pml4t_vbase};
    unsigned long offset = vaddr & 0xFFFFFFFFFFFFUL;
    unsigned long k;

    //释放页表 PT
    free_pages((void *) (ptt_vbase[(offset >> 12)] & PAGE_4K_MASK), page_num);
    memset(&ptt_vbase[(offset >> 12)], 0, page_num << 3);

    // 刷新 TLB
    for (unsigned long i = 0; i < page_num; i++) {
        INVLPG((vaddr & PAGE_4K_MASK) + i * 4096);
    }

    //释放页目录表PD 页目录指针表PDPT 四级页目录表PLM4
    for (unsigned long i = 0; i < 3; i++) {
        for (unsigned long j = 0; j < nums[i]; j++) {
            k = 0;
            while (table_bases[i][(offset >> level_offsets[i] << 9) + j * 512 + k] == 0) {
                if (k == 511) {
                    free_pages((void *) ((table_bases[i + 1][(offset >> level_offsets[i]) + j]) &
                                         PAGE_4K_MASK), 1);
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
void map_pages(unsigned long paddr, unsigned long vaddr, unsigned long page_num, unsigned long attr) {

    unsigned long nums[] = {
            ((page_num + ((vaddr >> 12) - ((vaddr >> 12) & ~(512UL * 512 * 512 - 1)))) +
             (512UL * 512 * 512 - 1)) / (512UL * 512 * 512),
            ((page_num + ((vaddr >> 12) - ((vaddr >> 12) & ~(512UL * 512 - 1)))) +
             (512UL * 512 - 1)) / (512UL * 512),
            ((page_num + ((vaddr >> 12) - ((vaddr >> 12) & ~(512UL - 1)))) + (512UL - 1)) / 512UL};
    unsigned long level_offsets[] = {39, 30, 21};
    unsigned long *table_bases[] = {pml4t_vbase, pdptt_vbase, pdt_vbase, ptt_vbase};
    unsigned long offset = vaddr & 0xFFFFFFFFFFFFUL;

    for (unsigned long i = 0; i < 3; i++) {
        for (unsigned long j = 0; j < nums[i]; j++) {
            if (table_bases[i][(offset >> level_offsets[i]) + j] == 0) {
                table_bases[i][(offset >> level_offsets[i]) + j] =
                        (unsigned long) alloc_pages(1) | (attr & 0x3F | PAGE_RW);
                memset(&table_bases[i + 1][(offset >> level_offsets[i] << 9) + j * 512], 0x0, 4096);
            }
        }
    }

    //PT 映射页表
    for (unsigned long i = 0; i < page_num; i++) {
        ptt_vbase[(offset >> 12) + i] = (paddr & PAGE_4K_MASK) + i * 4096 | attr;
        INVLPG((vaddr & PAGE_4K_MASK) + i * 4096);
    }

    return;
}


/*

//物理内存映射虚拟内存
void
map_pages11(unsigned long paddr, unsigned long vaddr, unsigned long page_num, unsigned long attr) {

    unsigned long num;
    unsigned long offset = vaddr & 0xFFFFFFFFFFFFUL;

    //PML4 映射四级页目录表
    num = ((page_num + ((vaddr >> 12) - ((vaddr >> 12) & ~(512UL * 512 * 512 - 1)))) +
           (512UL * 512 * 512 - 1)) / (512UL * 512 * 512);
    for (unsigned long i = 0; i < num; i++) {
        if (pml4t_vbase[(offset >> 39) + i] == 0) {
            pml4t_vbase[(offset >> 39) + i] =
                    (unsigned long) alloc_pages(1) | (attr & 0x3F | PAGE_RW);
            memset(&pdptt_vbase[(offset >> 39 << 9) + i * 512], 0x0, 4096);
        }
    }

    //PDPT 映射页目录指针表
    num = ((page_num + ((vaddr >> 12) - ((vaddr >> 12) & ~(512UL * 512 - 1)))) +
           (512UL * 512 - 1)) /
          (512UL * 512);
    for (unsigned long i = 0; i < num; i++) {
        if (pdptt_vbase[(offset >> 30) + i] == 0) {
            pdptt_vbase[(offset >> 30) + i] =
                    (unsigned long) alloc_pages(1) | (attr & 0x13F | PAGE_RW);
            memset(&pdt_vbase[(offset >> 30 << 9) + i * 512], 0x0, 4096);
        }
    }

    //PD 映射页目录表
    num = ((page_num + ((vaddr >> 12) - ((vaddr >> 12) & ~(512UL - 1)))) + (512UL - 1)) / 512UL;
    for (unsigned long i = 0; i < num; i++) {
        if (pdt_vbase[(offset >> 21) + i] == 0) {
            pdt_vbase[(offset >> 21) + i] =
                    (unsigned long) alloc_pages(1) | (attr & 0x13F | PAGE_RW);
            memset(&ptt_vbase[(offset >> 21 << 9) + i * 512], 0x0, 4096);
        }
    }

    //PT 映射页表
    for (unsigned long i = 0; i < page_num; i++) {
        ptt_vbase[(offset >> 12) + i] = (paddr & PAGE_4K_MASK) + i * 4096 | attr;
        INVLPG((vaddr & PAGE_4K_MASK) + i * 4096);
    }

    return;
}


//释放物理内存映射虚拟内存
void unmap_pages11(unsigned long vaddr, unsigned long page_num) {
    unsigned long num, j;
    unsigned long offset = vaddr & 0xFFFFFFFFFFFFUL;

    //释放页表 PT
    free_pages((void *) (ptt_vbase[(offset >> 12)] & PAGE_4K_MASK), page_num);
    memset(&ptt_vbase[(offset >> 12)], 0, page_num << 3);

    // 刷新 TLB
    for (unsigned long i = 0; i < page_num; i++) {
        INVLPG((vaddr & PAGE_4K_MASK) + i * 4096);
    }

    //释放页目录 PD
    num = ((page_num + ((vaddr >> 12) - ((vaddr >> 12) & ~(512UL - 1)))) + (512UL - 1)) / 512UL;
    for (unsigned long i = 0; i < num; i++) {
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
    num = ((page_num + ((vaddr >> 12) - ((vaddr >> 12) & ~(512UL * 512 - 1)))) +
           (512UL * 512 - 1)) /
          (512UL * 512);
    for (unsigned long i = 0; i < num; i++) {
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
    num = ((page_num + ((vaddr >> 12) - ((vaddr >> 12) & ~(512UL * 512 * 512 - 1)))) +
           (512UL * 512 * 512 - 1)) / (512UL * 512 * 512);
    for (unsigned long i = 0; i < num; i++) {
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
