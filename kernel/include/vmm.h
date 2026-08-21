#pragma once

#include "moslib.h"
#include "../init/linkage.h"

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
#define PAGE_4K_ALIGN(ADDR)    (((uint64)(ADDR) + PAGE_4K_SIZE - 1) & PAGE_4K_MASK)

//2M页表
#define PAGE_2M_SHIFT    21
#define PAGE_2M_SIZE    (1UL << PAGE_2M_SHIFT)
#define PAGE_2M_MASK    (~(PAGE_2M_SIZE - 1))
#define PAGE_2M_ALIGN(ADDR)     (((uint64)(ADDR) + PAGE_2M_SIZE - 1) & PAGE_2M_MASK)

//1G页表
#define PAGE_1G_SHIFT    30
#define PAGE_1G_SIZE    (1UL << PAGE_1G_SHIFT)
#define PAGE_1G_MASK    (~(PAGE_1G_SIZE - 1))
#define PAGE_1G_ALIGN(ADDR)     (((uint64)(ADDR) + PAGE_1G_SIZE - 1) & PAGE_1G_MASK)

//页属性
#define PAGE_NX     (1UL<<63)     // No-Execute (不可执行防漏洞，现代安全基石)
#define PAGE_G      (1UL<<8)      // Global (全局页，切换 CR3 时不刷新该 TLB)
#define PAGE_PS     (1UL<<7)      // Page Size (用于 PDE/PDPTE，1代表启用 2MB/1GB 巨页)
#define PAGE_PAT    (1UL<<7)      // Page Attribute Table (PAT 位，改变缓存规则)
#define PAGE_HUGE_PAT (1UL<<12)   // 2M 1G大页的PAT位移动到了12位
#define PAGE_D      (1UL<<6)      // Dirty (脏位，CPU写数据时自动置1)
#define PAGE_A      (1UL<<5)      // Accessed (访问位，CPU硬件自动置1)
#define PAGE_PCD    (1UL<<4)      // Page-Level Cache Disable (配合 PAT 使用)
#define PAGE_PWT    (1UL<<3)      // Page-Level Write-Through (配合 PAT 使用)
#define PAGE_US     (1UL<<2)      // User/Supervisor (用户态/内核态，1为允许Ring3访问)
#define PAGE_RW     (1UL<<1)      // Read/Write (读写权限，0为只读)
#define PAGE_P      (1UL<<0)      // Present (存在位)

// 基础缓存模式组合 
#define CACHE_WB    0                           // 回写 (普通内存)
#define CACHE_WC    (PAGE_PAT | PAGE_PCD)       // 写合并 (WC)
#define CACHE_WUC   (PAGE_PCD)                  // 弱不可缓存 (UC-)
#define CACHE_UC    (PAGE_PCD | PAGE_PWT)       // 强不可缓存 (Strong UC)

// --- 内核态 (Ring 0) 常用属性 ---
#define PAGE_KERNEL         (PAGE_G | PAGE_NX | PAGE_RW | PAGE_P | CACHE_WB) // 1. 普通内核数据/堆栈
#define PAGE_KERNEL_RX      (PAGE_G | PAGE_RW | PAGE_P | CACHE_WB)           // 内核代码段 (无NX)
#define PAGE_KERNEL_RO      (PAGE_G | PAGE_NX | PAGE_P | CACHE_WB)           // 内核只读数据
#define PAGE_KERNEL_WUC     (PAGE_G | PAGE_NX | PAGE_RW | PAGE_P | CACHE_WUC) // 2. 普通外设 IO (顺从 MTRR，安全兜底)
#define PAGE_KERNEL_WC      (PAGE_G | PAGE_NX | PAGE_RW | PAGE_P | CACHE_WC) // 4. 高吞吐 IO (强行聚合并发)
#define PAGE_KERNEL_UC      (PAGE_G | PAGE_NX | PAGE_RW | PAGE_P | CACHE_UC) // 3. 暴力外设 IO (无视 MTRR，六亲不认的强制直达！)

// --- 用户态 (Ring 3) 常用属性 ---
#define PAGE_USER           (PAGE_NX | PAGE_US | PAGE_RW | PAGE_P | CACHE_WB)    // 用户堆栈/数据
#define PAGE_USER_RX        (PAGE_US | PAGE_P | CACHE_WB)                        // 用户代码段
#define PAGE_USER_RO        (PAGE_NX | PAGE_US | PAGE_P | CACHE_WB)              // 用户只读数据

#define PML4E_SHIFT 39  // PML4E 索引的位移量
#define PDPTE_SHIFT 30  // PDPTE 索引的位移量
#define PDE_SHIFT 21    // PDE 索引的位移量
#define PTE_SHIFT 12    // PTE 索引的位移量

// 对齐函数，确保 addr 按 align 对齐（align 为 2 的幂）
static inline uint64 align_up(uint64 addr, uint64 align) {
    return addr + (align - 1) & -align;
}

static inline uint64 align_down(uint64 addr, uint64 align) {
    return addr & -align;
}

//虚拟地址转物理地址
static inline uint64 va_to_pa(void *va) {
    return (uint64)va & ~DIRECT_MAP_OFFSET;
}

//物理地址转虚拟地址
static inline void *pa_to_va(uint64 pa) {
    return (void *)(pa | DIRECT_MAP_OFFSET);
}

// 计算 PML4E 索引
static inline uint32 get_pml4e_index(uint64 va)
{
    return (va >> PML4E_SHIFT) & 0x1FF;
}

// 计算 PDPTE 索引
static inline uint32 get_pdpte_index(uint64 va)
{
    return (va >> PDPTE_SHIFT) & 0x1FF;
}

// 计算 PDE 索引
static inline uint32 get_pde_index(uint64 va)
{
    return (va >> PDE_SHIFT) & 0x1FF;
}

// 计算 PTE 索引
static inline uint32 get_pte_index(uint64 va)
{
    return (va >> PTE_SHIFT) & 0x1FF;
}

int32 vmmap(uint64 *pml4t, uint64 pa, void *va, uint64 attr, uint64 page_size);
int32 unvmmap(uint64 *pml4t, void *va);
int32 vmmap_range(uint64 *pml4t, uint64 pa, void *start_va, uint64 length, uint64 attr);
int32 unvmmap_range(uint64 *pml4t, void *start_va, uint64 size);
uint64 vmm_get_pmm(uint64 *pml4t, void *va);


// 2. 虚拟地址空间上下文 (Address Space Context)
// 代表一个进程的完整内存宇宙
typedef struct vm_space {
    uint64 pml4_phys;       // 该空间的顶层页表 (PML4) 的纯物理地址！(准备写给 CR3 寄存器)
    uint64 *pml4_virt;      // 内核访问 PML4 表的虚拟指针 (位于全局线性映射区)
    //vm_area *mmap;          // 属于该空间的所有 VMA 链表
    // spinlock_t lock;       // 保护该空间的自旋锁/读写锁
} vm_space_t;

/* *
 * vmm_map_page() (最底层的造桥函数)
 * 用途：将虚拟地址 vaddr 映射到物理地址 paddr。
 * 智能翻译：内部必须将软件的 PROT_XXX 转换为 PTE_RW/PTE_US/PTE_NX，
 * 同时将 attr (WB/WC/UC) 翻译为相应的 PTE_PAT/PTE_PCD/PTE_PWT 位组合。
 */
// int vmm_map_page(vm_space_t *space, uint64_t vaddr, uint64_t paddr,
//                  uint32_t prot_flags, mem_attr_t attr);

/* *
 * vmm_unmap_page()
 * 用途：拆除映射。
 * 关键：必须执行 `invlpg [vaddr]` 汇编指令来刷新 CPU 的本地 TLB！
 */
// void vmm_unmap_page(vm_space_t *space, uint64_t vaddr);

/* *
 * vmm_get_phys() (反向侦察兵)
 * 用途：给出一个 vaddr，遍历页表返回它的真实物理地址。
 * 实战：你的 NovaUSB 在设置 DMA / IOMMU 时，急需这个函数来确认 CPU 虚拟地址背后的物理真相。
 */
// uint64_t vmm_get_phys(vm_space_t *space, uint64_t vaddr);

/* *
 * vmm_map_huge_page_2M()
 * 用途：一次性映射 2MB 的连续物理内存。
 * 动作：在页目录项 (PDE) 层级直接打上 PTE_PS (Page Size) 位，不再往下查次级页表。
 * 优势：节省页表内存，TLB 命中率暴涨 512 倍。
 */
// int vmm_map_huge_page_2M(vm_space_t *space, uint64_t vaddr, uint64_t paddr,
//                          uint32_t prot_flags, mem_attr_t attr);


// 缺页异常硬件错误码解析宏 (来源于 CPU 压入栈的 Error Code)
#define PF_ERR_PRESENT  (1 << 0)  // 0=页不存在，1=页存在但权限冲突(如写只读页)
#define PF_ERR_WRITE    (1 << 1)  // 0=因读触发，1=因写触发
#define PF_ERR_USER     (1 << 2)  // 0=内核态触发，1=用户态进程触发
#define PF_ERR_INSTR    (1 << 4)  // 1=因取指令触发 (如触发了 NX 位保护)

/* *
 * vmm_page_fault_handler()
 * 用途：注册进 IDT(中断描述符表)的第 14 号中断处理函数。
 * 核心逻辑：
 * 1. 从 CR2 寄存器读出引发异常的虚拟地址 (Faulting Address)。
 * 2. 遍历当前 space->mmap，看这个地址合不合法。
 * 3. 如果合法但没映射(Demand Paging)：找伙伴系统要一页物理内存，调 vmm_map_page() 填进去，返回继续执行。
 * 4. 如果是写只读页(COW)：拷贝一页物理内存，改写页表权限为 RW。
 * 5. 如果是非法访问：发 SIGSEGV 信号杀掉用户进程 (Segmentation Fault)，或若是内核触发则 Kernel Panic。
 */
// void vmm_page_fault_handler(registers_t *regs, uint32_t err_code);

