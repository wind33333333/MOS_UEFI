#ifndef __MEMORY_H__
#define __MEMORY_H__

#include "printk.h"
#include "moslib.h"
#include "cpu.h"

void memoryInit(UINT8 bspFlags);

void *allocPages(UINT64 pageNumber);

int freePages(void *pagesAddr, UINT64 pageNumber);

void
mapPages(UINT64 phyAddr, UINT64 virAddr, UINT64 pageNumber, UINT64 attr);

void unMapPages(UINT64 phyAddr, UINT64 pageNumber);

extern UINT64 kenelstack_top;
extern UINT64 _start_text;
extern UINT64 __PML4T[512];

#define E820_SIZE    0x500
#define E820_BASE    0x504

#define PAGE_OFFSET    ((UINT64)0xffff800000000000)
#define PAGE_4K_SHIFT    12
#define PAGE_4K_SIZE    (1UL << PAGE_4K_SHIFT)
#define PAGE_4K_MASK    (~ (PAGE_4K_SIZE - 1))
#define PAGE_4K_ALIGN(addr)    (((UINT64)(addr) + PAGE_4K_SIZE - 1) & PAGE_4K_MASK)

#define HADDR_TO_LADDR(addr)    ((UINT64)(addr) & (~PAGE_OFFSET))
#define LADDR_TO_HADDR(addr)    ((UINT64 *)((UINT64)(addr) | PAGE_OFFSET))

typedef struct{
    UINT64 address;
    UINT64 length;
    UINT32 type;
} __attribute__((packed)) E820;

typedef struct {
    E820 e820[12];
    UINT64 e820Length;

    UINT64 *bitsMap;
    UINT64 bitsSize;
    UINT64 bitsLength;

    UINT64 totalPages;
    UINT64 allocPages;
    UINT64 freePages;

    UINT64 kernelStartAddress;
    UINT64 kernelEndAddress;

    UINT8 lock;
} GlobalMemoryDescriptor;

GlobalMemoryDescriptor memoryManagement = {0};

UINT64 *pml4tVirBase = (UINT64 *) 0xFFFFFFFFFFFFF000;  //pml4虚拟地址基址
UINT64 *pdpttVirBase = (UINT64 *) 0xFFFFFFFFFFE00000;  //pdpt虚拟地址基址
UINT64 *pdtVirBase = (UINT64 *) 0xFFFFFFFFC0000000;    //pd虚拟地址基址
UINT64 *pttVirBase= (UINT64 *) 0xFFFFFF8000000000;    //pt虚拟地址基址


#define MFENCE() __asm__ __volatile__ ("mfence":::);
#define LFENCE() __asm__ __volatile__ ("lfence":::);
#define SFENCE() __asm__ __volatile__ ("sfence":::);
#define INVLPG(virAddr) __asm__ __volatile__("invlpg (%0)"::"r"(virAddr):);
#define SET_CR3(phyAddr) __asm__ __volatile__("mov %0,%%cr3"::"r"(phyAddr):);
#define GET_CR3(phyAddr) __asm__ __volatile__("mov %%cr3,%0":"=r"(phyAddr)::);


#define PAGE_NX     1UL<<63
#define PAGE_G      1UL<<8
#define PAGE_PAT    1UL<<7
#define PAGE_D      1UL<<6
#define PAGE_A      1UL<<5
#define PAGE_PCD    1UL<<4
#define PAGE_PWT    1UL<<3
#define PAGE_US     1UL<<2
#define PAGE_RW     1UL<<1
#define PAGE_P      1UL<<0


#define PAGE_UC          (PAGE_NX | PAGE_G | PAGE_PAT | PAGE_PCD | PAGE_PWT | PAGE_RW | PAGE_P)       //可读可写 内存不可缓存，对设备内存IO映射非常有用
#define PAGE_ROOT_R      (PAGE_NX | PAGE_G | PAGE_PAT | PAGE_P)                                       //只读
#define PAGE_ROOT_RW     (PAGE_NX | PAGE_G | PAGE_PAT | PAGE_RW |PAGE_P)                              //可读可写 内存写回，对普通内存映射非常有用
#define PAGE_ROOT_RX     (PAGE_G | PAGE_PAT | PAGE_P)                                                 //只读可执行
#define PAGE_ROOT_RWX    (PAGE_G | PAGE_PAT | PAGE_RW |PAGE_P)                                        //可读可写可执行

#define PAGE_USER_R      (PAGE_NX | PAGE_PAT | PAGE_US | PAGE_P)                      //可读                                                      //只读
#define PAGE_USER_RW     (PAGE_NX | PAGE_PAT | PAGE_US | PAGE_RW | PAGE_P)            //可读可写
#define PAGE_USER_RX     (PAGE_PAT | PAGE_US | PAGE_P)                                //可读可执行
#define PAGE_USER_RWX    (PAGE_PAT | PAGE_US | PAGE_RW | PAGE_P)                      //可读可写可执行

#endif
