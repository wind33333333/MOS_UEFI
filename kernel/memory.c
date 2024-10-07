#include "memory.h"

__attribute__((section(".init_text"))) void memoryInit(UINT8 bspFlags) {
    if (bspFlags) {
        UINT32 x = 0;
        UINT64 totalMem = 0;
        E820 *p = (E820 *) E820_BASE;
        for (UINT32 i = 0; i < *(UINT32 *) E820_SIZE; i++) {
            colorPrintK(ORANGE, BLACK, "Addr: %#018lX\t Len: %#018lX\t Type: %d\n", p->address,p->length, p->type);
            if (p->type == 1) {
                memoryManagement.e820[x].address = p->address & PAGE_4K_MASK;
                memoryManagement.e820[x].length = p->length & PAGE_4K_MASK;
                memoryManagement.e820[x].type = p->type;
                memoryManagement.e820Length++;
                totalMem += p->length;
                memoryManagement.totalPages +=
                        memoryManagement.e820[x].length >> PAGE_4K_SHIFT;
                x++;
            }
            p++;
        }
        colorPrintK(ORANGE, BLACK, "OS Can Used Total RAM: %#018lX=%ldMB\n", totalMem,
                     totalMem / 1024 / 1024);

        //bits map construction init
        totalMem = memoryManagement.e820[memoryManagement.e820Length - 1].address +
                   memoryManagement.e820[memoryManagement.e820Length - 1].length;
        memoryManagement.bitsMap = (UINT64 *) kenelstack_top;
        memoryManagement.bitsSize = totalMem >> PAGE_4K_SHIFT;
        memoryManagement.bitsLength =
                (memoryManagement.bitsSize + 63) / 8 & 0xFFFFFFFFFFFFFFF8UL;
        memSet(memoryManagement.bitsMap, 0xff, memoryManagement.bitsLength);

        //bit map 1M以上可用空间置0，i=1跳过1M保持使用置1，等全部初始化后再释放
        for (UINT32 i = 1; i < memoryManagement.e820Length; i++) {
            memSet(memoryManagement.bitsMap +
                   ((memoryManagement.e820[i].address >> PAGE_4K_SHIFT) >> 6), 0,
                   (memoryManagement.e820[i].length >> PAGE_4K_SHIFT) >> 3);
            totalMem = memoryManagement.e820[i].address +
                       memoryManagement.e820[i].length & 0xFFFFFFFFFFFF8000UL;
            for (; totalMem < (memoryManagement.e820[i].address +
                               memoryManagement.e820[i].length); totalMem += PAGE_4K_SIZE) {
                *(memoryManagement.bitsMap + (totalMem >> PAGE_4K_SHIFT >> 6)) ^=
                        1UL << (totalMem >> PAGE_4K_SHIFT) % 64;
            }
        }

        //kernelEndAddress结束地址加上bit map对齐4K地址
        memoryManagement.kernelStartAddress = (UINT64) &_start_text;
        memoryManagement.kernelEndAddress =
                kenelstack_top + (memoryManagement.bitsLength + 0xfff) &
                0xFFFFFFFFFFFFF000UL;

        //把内核1M开始到kernelEndAddress地址bit map置1，标记为已使用
        memSet(memoryManagement.bitsMap + ((0x100000 >> PAGE_4K_SHIFT) >> 6), 0xFF,
               (HADDR_TO_LADDR(memoryManagement.kernelEndAddress) - 0x100000) >> PAGE_4K_SHIFT
                                                                                >> 3);
        totalMem = HADDR_TO_LADDR(memoryManagement.kernelEndAddress) & 0xFFFFFFFFFFFF8000UL;
        for (; totalMem <
               HADDR_TO_LADDR(memoryManagement.kernelEndAddress); totalMem += PAGE_4K_SIZE) {
            *(memoryManagement.bitsMap + (totalMem >> PAGE_4K_SHIFT >> 6)) ^=
                    1UL << ((totalMem - 0x100000) >> PAGE_4K_SHIFT) % 64;
        }

        memoryManagement.allocPages += (memoryManagement.e820[0].length
                >> PAGE_4K_SHIFT);
        memoryManagement.allocPages += (
                (HADDR_TO_LADDR(memoryManagement.kernelEndAddress) - 0x100000) >> PAGE_4K_SHIFT);
        memoryManagement.freePages =
                memoryManagement.totalPages - memoryManagement.allocPages;

        colorPrintK(ORANGE, BLACK,
                     "bitsMap: %#018lX \tbitsSize: %#018lX \tbitsLength: %#018lX\n",
                     memoryManagement.bitsMap, memoryManagement.bitsSize,
                     memoryManagement.bitsLength);
        colorPrintK(ORANGE, BLACK, "OS Can Used Total 4K PAGEs: %ld \tAlloc: %ld \tFree: %ld\n",
                     memoryManagement.totalPages, memoryManagement.allocPages,
                     memoryManagement.freePages);
    }
    return;
}


//物理页分配器
void *allocPages(UINT64 pageNumber) {
    SPIN_LOCK(memoryManagement.lock);

    if (memoryManagement.freePages < pageNumber || pageNumber == 0) {
        memoryManagement.lock = 0;
        return (void *) -1;
    }

    UINT64 start_idx = 0;
    UINT64 current_length = 0;

    for (UINT64 i = 0; i < (memoryManagement.bitsSize / 64); i++) {
        if ((memoryManagement.bitsMap[i] == 0xFFFFFFFFFFFFFFFFUL)) {
            current_length = 0;
            continue;
        }

        for (UINT64 j = 0; j < 64; j++) {
            if (memoryManagement.bitsMap[i] & (1UL << j)) {
                current_length = 0;
                continue;
            }

            if (current_length == 0)
                start_idx = i * 64 + j;

            current_length++;

            if (current_length == pageNumber) {
                for (UINT64 y = 0; y < pageNumber; y++) {
                    (memoryManagement.bitsMap[(start_idx + y) / 64] |= (1UL
                            << ((start_idx + y) % 64)));
                }

                memoryManagement.allocPages += pageNumber;
                memoryManagement.freePages -= pageNumber;
                memoryManagement.lock = 0;
                return (void *) (start_idx << PAGE_4K_SHIFT); // 找到连续空闲块，返回起始索引
            }
        }
    }

    memoryManagement.lock = 0;
    return (void *) -1; // 没有找到足够大的连续内存块
}


//物理页释放器
int freePages(void *pagesAddr, UINT64 pageNumber) {
    SPIN_LOCK(memoryManagement.lock);
    if ((UINT64)(pagesAddr + (pageNumber << PAGE_4K_SHIFT)) >
        (memoryManagement.e820[memoryManagement.e820Length - 1].address +
         memoryManagement.e820[memoryManagement.e820Length - 1].length)) {
        memoryManagement.lock = 0;
        return -1;
    }

    for (UINT64 i = 0; i < pageNumber; i++) {
        (memoryManagement.bitsMap[(((UINT64) pagesAddr >> PAGE_4K_SHIFT) + i) /
                                           64] ^= (1UL
                << (((UINT64) pagesAddr >> PAGE_4K_SHIFT) + i) % 64));
    }
    memoryManagement.allocPages -= pageNumber;
    memoryManagement.freePages += pageNumber;
    memoryManagement.lock = 0;
    return 0;
}


//释放物理内存映射虚拟内存
void unMapPages(UINT64 virAddr, UINT64 pageNumber) {
    UINT64 nums[] = {
            ((pageNumber + ((virAddr >> 12) - ((virAddr >> 12) & ~(512UL - 1)))) + (512UL - 1)) / 512UL,
            ((pageNumber + ((virAddr >> 12) - ((virAddr >> 12) & ~(512UL * 512 - 1)))) +
             (512UL * 512 - 1)) / (512UL * 512),
            ((pageNumber + ((virAddr >> 12) - ((virAddr >> 12) & ~(512UL * 512 * 512 - 1)))) +
             (512UL * 512 * 512 - 1)) / (512UL * 512 * 512)};
    UINT64 levelOffsets[] = {21, 30, 39};
    UINT64 *tableBases[] = {pttVirBase, pdtVirBase, pdpttVirBase, pml4tVirBase};
    UINT64 offset = virAddr & 0xFFFFFFFFFFFFUL;
    UINT64 k;

    //释放页表 PT
    freePages((void *) (pttVirBase[(offset >> 12)] & PAGE_4K_MASK), pageNumber);
    memSet(&pttVirBase[(offset >> 12)], 0, pageNumber << 3);

    // 刷新 TLB
    for (UINT64 i = 0; i < pageNumber; i++) {
        INVLPG((virAddr & PAGE_4K_MASK) + i * 4096);
    }

    //释放页目录表PD 页目录指针表PDPT 四级页目录表PLM4
    for (UINT64 i = 0; i < 3; i++) {
        for (UINT64 j = 0; j < nums[i]; j++) {
            k = 0;
            while (tableBases[i][(offset >> levelOffsets[i] << 9) + j * 512 + k] == 0) {
                if (k == 511) {
                    freePages((void *) ((tableBases[i + 1][(offset >> levelOffsets[i]) + j]) &
                                         PAGE_4K_MASK), 1);
                    tableBases[i + 1][(offset >> levelOffsets[i]) + j] = 0;
                    break;
                }
                k++;
            }
        }
    }

    return;
}


//物理内存映射虚拟内存
void mapPages(UINT64 paddr, UINT64 virAddr, UINT64 pageNumber, UINT64 attr) {

    UINT64 nums[] = {
            ((pageNumber + ((virAddr >> 12) - ((virAddr >> 12) & ~(512UL * 512 * 512 - 1)))) +
             (512UL * 512 * 512 - 1)) / (512UL * 512 * 512),
            ((pageNumber + ((virAddr >> 12) - ((virAddr >> 12) & ~(512UL * 512 - 1)))) +
             (512UL * 512 - 1)) / (512UL * 512),
            ((pageNumber + ((virAddr >> 12) - ((virAddr >> 12) & ~(512UL - 1)))) + (512UL - 1)) / 512UL};
    UINT64 levelOffsets[] = {39, 30, 21};
    UINT64 *tableBases[] = {pml4tVirBase, pdpttVirBase, pdtVirBase, pttVirBase};
    UINT64 offset = virAddr & 0xFFFFFFFFFFFFUL;

    for (UINT64 i = 0; i < 3; i++) {
        for (UINT64 j = 0; j < nums[i]; j++) {
            if (tableBases[i][(offset >> levelOffsets[i]) + j] == 0) {
                tableBases[i][(offset >> levelOffsets[i]) + j] =
                        (UINT64) allocPages(1) | (attr & 0x3F | PAGE_RW);
                memSet(&tableBases[i + 1][(offset >> levelOffsets[i] << 9) + j * 512], 0x0, 4096);
            }
        }
    }

    //PT 映射页表
    for (UINT64 i = 0; i < pageNumber; i++) {
        pttVirBase[(offset >> 12) + i] = (paddr & PAGE_4K_MASK) + i * 4096 | attr;
        INVLPG((virAddr & PAGE_4K_MASK) + i * 4096);
    }

    return;
}


/*

//物理内存映射虚拟内存
void
mapPages11(UINT64 paddr, UINT64 virAddr, UINT64 pageNumber, UINT64 attr) {

    UINT64 num;
    UINT64 offset = virAddr & 0xFFFFFFFFFFFFUL;

    //PML4 映射四级页目录表
    num = ((pageNumber + ((virAddr >> 12) - ((virAddr >> 12) & ~(512UL * 512 * 512 - 1)))) +
           (512UL * 512 * 512 - 1)) / (512UL * 512 * 512);
    for (UINT64 i = 0; i < num; i++) {
        if (pml4tVirBase[(offset >> 39) + i] == 0) {
            pml4tVirBase[(offset >> 39) + i] =
                    (UINT64) allocPages(1) | (attr & 0x3F | PAGE_RW);
            memSet(&pdpttVirBase[(offset >> 39 << 9) + i * 512], 0x0, 4096);
        }
    }

    //PDPT 映射页目录指针表
    num = ((pageNumber + ((virAddr >> 12) - ((virAddr >> 12) & ~(512UL * 512 - 1)))) +
           (512UL * 512 - 1)) /
          (512UL * 512);
    for (UINT64 i = 0; i < num; i++) {
        if (pdpttVirBase[(offset >> 30) + i] == 0) {
            pdpttVirBase[(offset >> 30) + i] =
                    (UINT64) allocPages(1) | (attr & 0x13F | PAGE_RW);
            memSet(&pdtVirBase[(offset >> 30 << 9) + i * 512], 0x0, 4096);
        }
    }

    //PD 映射页目录表
    num = ((pageNumber + ((virAddr >> 12) - ((virAddr >> 12) & ~(512UL - 1)))) + (512UL - 1)) / 512UL;
    for (UINT64 i = 0; i < num; i++) {
        if (pdtVirBase[(offset >> 21) + i] == 0) {
            pdtVirBase[(offset >> 21) + i] =
                    (UINT64) allocPages(1) | (attr & 0x13F | PAGE_RW);
            memSet(&pttVirBase[(offset >> 21 << 9) + i * 512], 0x0, 4096);
        }
    }

    //PT 映射页表
    for (UINT64 i = 0; i < pageNumber; i++) {
        pttVirBase[(offset >> 12) + i] = (paddr & PAGE_4K_MASK) + i * 4096 | attr;
        INVLPG((virAddr & PAGE_4K_MASK) + i * 4096);
    }

    return;
}


//释放物理内存映射虚拟内存
void unMapPages11(UINT64 virAddr, UINT64 pageNumber) {
    UINT64 num, j;
    UINT64 offset = virAddr & 0xFFFFFFFFFFFFUL;

    //释放页表 PT
    freePages((void *) (pttVirBase[(offset >> 12)] & PAGE_4K_MASK), pageNumber);
    memSet(&pttVirBase[(offset >> 12)], 0, pageNumber << 3);

    // 刷新 TLB
    for (UINT64 i = 0; i < pageNumber; i++) {
        INVLPG((virAddr & PAGE_4K_MASK) + i * 4096);
    }

    //释放页目录 PD
    num = ((pageNumber + ((virAddr >> 12) - ((virAddr >> 12) & ~(512UL - 1)))) + (512UL - 1)) / 512UL;
    for (UINT64 i = 0; i < num; i++) {
        j = 0;
        while (pttVirBase[(offset >> 21 << 9) + i * 512 + j] == 0) {
            if (j == 511) {
                freePages((void *) (pdtVirBase[(offset >> 21) + i] & PAGE_4K_MASK), 1);
                pdtVirBase[(offset >> 21) + i] = 0;
                break;
            }
            j++;
        }
    }

    //释放页目录表 PDPT
    num = ((pageNumber + ((virAddr >> 12) - ((virAddr >> 12) & ~(512UL * 512 - 1)))) +
           (512UL * 512 - 1)) /
          (512UL * 512);
    for (UINT64 i = 0; i < num; i++) {
        j = 0;
        while (pdtVirBase[(offset >> 30 << 9) + i * 512UL + j] == 0) {
            if (j == 511) {
                freePages((void *) (pdpttVirBase[(offset >> 30) + i] & PAGE_4K_MASK), 1);
                pdpttVirBase[(offset >> 30) + i] = 0;
                break;
            }
            j++;
        }
    }

    //释放页目录表 PML4T
    num = ((pageNumber + ((virAddr >> 12) - ((virAddr >> 12) & ~(512UL * 512 * 512 - 1)))) +
           (512UL * 512 * 512 - 1)) / (512UL * 512 * 512);
    for (UINT64 i = 0; i < num; i++) {
        j = 0;
        while (pdpttVirBase[(offset >> 39 << 9) + i * 512UL + j] == 0) {
            if (j == 511) {
                freePages((void *) (pml4tVirBase[(offset >> 39) + i] & PAGE_4K_MASK), 1);
                pml4tVirBase[(offset >> 39) + i] = 0;
                break;
            }
            j++;
        }
    }

    return;
}

*/
