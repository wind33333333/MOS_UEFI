#pragma once

#include "moslib.h"
#include "linkage.h"

//页级别
typedef enum {
    pml4e_level=4,
    pdpte_level=3,
    pde_level=2,
    pte_level=1
}page_level_e;

//页表项物理地址掩码
#define PAGE_PA_MASK    0x7FFFFFFFF000UL

//4K页表
#define PAGE_4K_SHIFT    12
#define PAGE_4K_SIZE    (1UL << PAGE_4K_SHIFT)
#define PAGE_4K_MASK    (~(PAGE_4K_SIZE - 1))
#define PAGE_4K_ALIGN(ADDR)    (((UINT64)(ADDR) + PAGE_4K_SIZE - 1) & PAGE_4K_MASK)

//2M页表
#define PAGE_2M_SHIFT    21
#define PAGE_2M_SIZE    (1UL << PAGE_2M_SHIFT)
#define PAGE_2M_MASK    (~(PAGE_2M_SIZE - 1))
#define PAGE_2M_ALIGN(ADDR)     (((UINT64)(ADDR) + PAGE_2M_SIZE - 1) & PAGE_2M_MASK)

//1G页表
#define PAGE_1G_SHIFT    30
#define PAGE_1G_SIZE    (1UL << PAGE_1G_SHIFT)
#define PAGE_1G_MASK    (~(PAGE_1G_SIZE - 1))
#define PAGE_1G_ALIGN(ADDR)     (((UINT64)(ADDR) + PAGE_1G_SIZE - 1) & PAGE_1G_MASK)

//页属性
#define PAGE_NX     1UL<<63
#define PAGE_G      1UL<<8
#define PAGE_PS     1UL<<7      //pdpte第7位ps位置位表示1G巨页 pde第7位ps位置位2M大页
#define PAGE_PAT    1UL<<7      //pte第7位
#define PAGE_D      1UL<<6
#define PAGE_A      1UL<<5
#define PAGE_PCD    1UL<<4
#define PAGE_PWT    1UL<<3
#define PAGE_US     1UL<<2
#define PAGE_RW     1UL<<1
#define PAGE_P      1UL<<0

#define PAGE_WB     0                         //回写普通内存
#define PAGE_UC     PAGE_PWT                  //不可缓存，要求严格顺序的内存映射 I/O
#define PAGE_WC     PAGE_PCD                  //写合并，聚合写入操作，优化写入性能 适合视频内存等批量写入场景
#define PAGE_WT     (PAGE_PWT|PAGE_PCD)       //写通
#define PAGE_WP     PAGE_PAT                  //读操作先访问缓存，写操作扩散到所有处理器
#define PAGE_UC_    (PAGE_PWT|PAGE_PAT)       //部分不可缓存

#define PAGE_ROOT_RWX_4K    (PAGE_G | PAGE_RW |PAGE_P | PAGE_WB)             //可读-可写-可执行
#define PAGE_ROOT_RX_4K     (PAGE_G | PAGE_P | PAGE_WB)                      //可读-可执行
#define PAGE_ROOT_R_4K      (PAGE_NX | PAGE_G | PAGE_P | PAGE_WB)            //只读
#define PAGE_ROOT_RW_4K     (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_WB)   //可读-可写-普通内存
#define PAGE_ROOT_RW_WC_4K  (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_WC)   //可读-可写-显卡内存
#define PAGE_ROOT_RW_UC_4K  (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_UC)   //可读-可写-IO映射内存

#define PAGE_ROOT_RWX_2M1G    (PAGE_G | PAGE_RW |PAGE_P | PAGE_WB | PAGE_PS)             //可读-可写-可执行
#define PAGE_ROOT_RX_2M1G     (PAGE_G | PAGE_P | PAGE_WB | PAGE_PS)                      //可读-可执行
#define PAGE_ROOT_R_2M1G      (PAGE_NX | PAGE_G | PAGE_P | PAGE_WB | PAGE_PS)            //只读
#define PAGE_ROOT_RW_2M1G     (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_WB | PAGE_PS)   //可读-可写-普通内存
#define PAGE_ROOT_RW_WC_2M1G  (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_WC | PAGE_PS)   //可读-可写-显卡内存
#define PAGE_ROOT_RW_UC_2M1G  (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_UC | PAGE_PS)   //可读-可写-IO映射内存

#define PAGE_USER_R      (PAGE_NX | PAGE_US | PAGE_P | PAGE_WB)              //只读
#define PAGE_USER_RW     (PAGE_NX | PAGE_US | PAGE_RW | PAGE_P | PAGE_WB)    //可读可写
#define PAGE_USER_RX     (PAGE_US | PAGE_P | PAGE_WB)                        //可读可执行
#define PAGE_USER_RWX    (PAGE_US | PAGE_RW | PAGE_P | PAGE_WB)              //可读可写可执行

#define PML4E_SHIFT 39  // PML4E 索引的位移量
#define PDPTE_SHIFT 30  // PDPTE 索引的位移量
#define PDE_SHIFT 21    // PDE 索引的位移量
#define PTE_SHIFT 12    // PTE 索引的位移量

// 对齐函数，确保 addr 按 align 对齐（align 为 2 的幂）
static inline UINT64 align_up(UINT64 addr, UINT64 align) {
    return addr + (align - 1) & -align;
}

static inline UINT64 align_down(UINT64 addr, UINT64 align) {
    return addr & -align;
}

//虚拟地址转物理地址
static inline UINT64 va_to_pa(void *va) {
    return (UINT64)va & ~DIRECT_MAP_OFFSET;
}

//物理地址转虚拟地址
static inline void *pa_to_va(UINT64 pa) {
    return (void *)(pa | DIRECT_MAP_OFFSET);
}

// 计算 PML4E 索引
static inline UINT32 get_pml4e_index(void *va)
{
    return ((UINT64)va >> PML4E_SHIFT) & 0x1FF;
}

// 计算 PDPTE 索引
static inline UINT32 get_pdpte_index(void *va)
{
    return ((UINT64)va >> PDPTE_SHIFT) & 0x1FF;
}

// 计算 PDE 索引
static inline UINT32 get_pde_index(void *va)
{
    return ((UINT64)va >> PDE_SHIFT) & 0x1FF;
}

// 计算 PTE 索引
static inline UINT32 get_pte_index(void *va)
{
    return ((UINT64)va >> PTE_SHIFT) & 0x1FF;
}

INT32 mmap(UINT64 *pml4t, UINT64 pa, void *va, UINT64 attr,UINT64 page_size);
INT32 unmmap(UINT64 *pml4t, void *va,UINT64 page_size);
INT32 mmap_range(UINT64 *pml4t, UINT64 pa, void *va, UINT64 size, UINT64 attr,UINT64 page_size);
INT32 unmmap_range(UINT64 *pml4t, void *va, UINT64 size, UINT64 page_size);
UINT64 find_page_table_entry(UINT64 *pml4t,void *va,UINT32 page_level);
UINT32 update_page_table_entry(UINT64 *pml4t, void *va, UINT32 page_level,UINT64 entry);

