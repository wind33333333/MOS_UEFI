#ifndef __MEMORY_H__
#define __MEMORY_H__

#include "printk.h"
#include "lib.h"
#include "cpu.h"

void memory_init(unsigned char bsp_flags);

void *alloc_pages(unsigned long required_length);

int free_pages(void *pages_addr, unsigned long required_length);

void
map_pages(unsigned long paddr, unsigned long vaddr, unsigned long page_num, unsigned long attr);

void unmap_pages(unsigned long paddr, unsigned long page_num);

extern unsigned long kenelstack_top;
extern unsigned long _start_text;
extern unsigned long __PML4T[512];

#define E820_SIZE    0x500
#define E820_BASE    0x504

#define PAGE_OFFSET    ((unsigned long)0xffff800000000000)
#define PAGE_4K_SHIFT    12
#define PAGE_4K_SIZE    (1UL << PAGE_4K_SHIFT)
#define PAGE_4K_MASK    (~ (PAGE_4K_SIZE - 1))
#define PAGE_4K_ALIGN(addr)    (((unsigned long)(addr) + PAGE_4K_SIZE - 1) & PAGE_4K_MASK)

#define HADDR_TO_LADDR(addr)    ((unsigned long)(addr) & (~PAGE_OFFSET))
#define LADDR_TO_HADDR(addr)    ((unsigned long *)((unsigned long)(addr) | PAGE_OFFSET))

struct E820 {
    unsigned long address;
    unsigned long length;
    unsigned int type;
}__attribute__((packed));

typedef struct {
    struct E820 e820[12];
    unsigned long e820_length;

    unsigned long *bits_map;
    unsigned long bits_size;
    unsigned long bits_length;

    unsigned long total_pages;
    unsigned long alloc_pages;
    unsigned long free_pages;

    unsigned long kernel_start;
    unsigned long kernel_end;

    unsigned char lock;
} Global_Memory_Descriptor;

Global_Memory_Descriptor memory_management_struct = {0};

unsigned long *pml4t_vbase = (unsigned long *) 0xFFFFFFFFFFFFF000;  //pml4虚拟地址基址
unsigned long *pdptt_vbase = (unsigned long *) 0xFFFFFFFFFFE00000;  //pdpt虚拟地址基址
unsigned long *pdt_vbase = (unsigned long *) 0xFFFFFFFFC0000000;    //pd虚拟地址基址
unsigned long *ptt_vbase = (unsigned long *) 0xFFFFFF8000000000;    //pt虚拟地址基址


#define MFENCE() __asm__ __volatile__ ("mfence":::);
#define LFENCE() __asm__ __volatile__ ("lfence":::);
#define SFENCE() __asm__ __volatile__ ("sfence":::);
#define INVLPG(vaddr) __asm__ __volatile__("invlpg (%0)"::"r"(vaddr):);
#define SET_CR3(paddr) __asm__ __volatile__("mov %0,%%cr3"::"r"(paddr):);
#define GET_CR3(paddr) __asm__ __volatile__("mov %%cr3,%0":"=r"(paddr)::);


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
