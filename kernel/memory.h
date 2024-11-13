#ifndef __MEMORY_H__
#define __MEMORY_H__
#include "moslib.h"

void init_memory(void);

void *alloc_pages(UINT64 page_number);

int free_pages(void *pages_addr, UINT64 page_number);

void
map_pages(UINT64 phy_addr, UINT64 vir_addr, UINT64 page_number, UINT64 attr);

void unmap_pages(UINT64 phy_addr, UINT64 page_number);

extern UINT64 *pml4t_vbase;  //pml4t虚拟地址基址
extern UINT64 *pdptt_vbase;  //pdptt虚拟地址基址
extern UINT64 *pdt_vbase;    //pdt虚拟地址基址
extern UINT64 *ptt_vbase;    //ptt虚拟地址基址

extern UINT64 kernel_stack_top;
extern CHAR8 _start_text[];
extern CHAR8 _start_init_text[];

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
}mem_map_t;

typedef struct {
    mem_map_t mem_map[20];
    UINT32 mem_map_number;
    UINT64 avl_mem_size;

    UINT64 *bitmap;
    UINT64 bitmap_size;
    UINT64 bitmap_length;

    UINT64 total_pages;
    UINT64 used_pages;
    UINT64 avl_pages;

    UINT64 kernel_start_address;
    UINT64 kernel_end_address;

    UINT8 lock;
} global_memory_descriptor_t;

extern global_memory_descriptor_t memory_management;

#define INVLPG(vir_addr) __asm__ __volatile__("invlpg (%0)"::"r"(vir_addr):"memory");

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


#define PAGE_ROOT_UC     (PAGE_NX | PAGE_G | PAGE_PAT | PAGE_PCD | PAGE_PWT | PAGE_RW | PAGE_P)       //可读可写 内存不可缓存，对设备内存IO映射非常有用
#define PAGE_ROOT_R      (PAGE_NX | PAGE_G | PAGE_PAT | PAGE_P)                                       //只读
#define PAGE_ROOT_RW     (PAGE_NX | PAGE_G | PAGE_PAT | PAGE_RW |PAGE_P)                              //可读可写 内存写回，对普通内存映射非常有用
#define PAGE_ROOT_RX     (PAGE_G | PAGE_PAT | PAGE_P)                                                 //只读可执行
#define PAGE_ROOT_RWX    (PAGE_G | PAGE_PAT | PAGE_RW |PAGE_P)                                        //可读可写可执行

#define PAGE_USER_R      (PAGE_NX | PAGE_PAT | PAGE_US | PAGE_P)                      //只读
#define PAGE_USER_RW     (PAGE_NX | PAGE_PAT | PAGE_US | PAGE_RW | PAGE_P)            //可读可写
#define PAGE_USER_RX     (PAGE_PAT | PAGE_US | PAGE_P)                                //可读可执行
#define PAGE_USER_RWX    (PAGE_PAT | PAGE_US | PAGE_RW | PAGE_P)                      //可读可写可执行

#endif
