#include "tlb.h"

//刷新单个虚拟地址TLB含全局页
static inline void asm_invlpg(uint64 va) {
    __asm__ __volatile__("invlpg (%0) \n\t" : : "r"(va) : "memory");
}

/**
 * @brief 执行 INVPCID 刷新 TLB
 * @param type 刷新类型 (0:一个虚拟地址普通页, 1:同一个pcid标记的所有普通页, 2:全部含全局页, 3:全部不含全局页)
 * @param pcid 目标 PCID (0 ~ 4095)
 * @param va   需要刷新的虚拟地址 (仅 Type 0 生效)
 */
static inline void asm_invpcid(uint64 type, uint64 pcid, uint64 va) {
    // 构造描述符，强制 16 字节对齐
    struct {
        uint64 pcid_resv;// 低 12 位是 PCID，高 52 位必须为 0
        uint64 addr;     // 需要刷新的虚拟地址
    } __attribute__((aligned(16))) invpcid_desc = {
        .pcid_resv = pcid & 0xFFF, // 确保高 52 位为 0
        .addr = va
    };

    // 内联汇编：
    // %0 对应内存变量 desc ("m")
    // %1 对应寄存器变量 type ("r")
    __asm__ __volatile__ (
        "invpcid %0, %1"
        :
        : "m" (invpcid_desc), "r" (type)
        : "memory" // 加上 memory 屏障，防止编译器重排指令
    );
}


// 批量刷新的性能阈值（Linux 经典调优值）
// 超过 32 页（128KB）的逐页刷新，性能不如直接清空整个 TLB
#define TLB_BATCH_FLUSH_MAX_PAGES 32 

/**
 * @brief 【API 1】刷新当前上下文的单个普通页/全局页
 * @param va 虚拟地址
 * @note  不管有没有 INVPCID，INVLPG 都是当前上下文中最高效的单页刷新指令
 */
static inline void tlb_flush_page(uint64 va) {
    // 无论是普通页还是 Global 页，INVLPG 都能精准作废它
    asm_invlpg(va);
}

/**
 * @brief 【API 2】刷新指定 PCID 进程的单个普通页
 * @param pcid 目标进程的 PCID
 * @param va   虚拟地址
 * @note  专为 SMP 跨核或后台回收内存设计。如果硬件不支持，优雅降级。
 */
static inline void tlb_flush_page_by_pcid(uint16 pcid, uint64 va) {
    if (g_cpu_has_invpcid) {
        // Type 0: 狙击指定 PCID 的特定地址
        asm_invpcid(0, pcid, va);
    } else {
        // 优雅降级：老硬件没有 PCID，所有进程共用 TLB
        // 直接用 INVLPG 刷掉当前地址即可
        asm_invlpg(va);
    }
}

/**
 * @brief 【API 4】清空当前进程的所有普通页 (保留全局页)
 * @note  常用于进程发生严重缺页、或者整体销毁重建等场景
 */
static inline void tlb_flush_local_all(void) {
    // 技巧：重载当前的 CR3，硬件会自动清空当前 PCID 的所有普通缓存
    // 这里没有用 INVPCID，因为 mov cr3 本质上就是当前上下文的 Type 1，效率极高
    __hw_flush_cr3();
}

/**
 * @brief 【API 3】批量范围刷新 (智能路由)
 * @param start_va 起始虚拟地址
 * @param size     刷新的总大小 (字节)
 * @note  如果页数较少，走循环单页刷新；如果范围极大，直接炸掉整个进程 TLB，效率更高。
 */
static inline void tlb_flush_range(uint64 start_va, uint64 size) {
    uint64 page_count = size / PAGE_4K_SIZE; // 假设最小粒度 4K
    
    if (page_count <= TLB_BATCH_FLUSH_MAX_PAGES) {
        // 范围很小：逐页狙击 (Sniper Mode)
        for (uint64 i = 0; i < page_count; i++) {
            asm_invlpg(start_va + i * PAGE_4K_SIZE);
        }
    } else {
        // 范围巨大：直接清空当前进程的所有非全局页 (Nuke Mode)
        tlb_flush_local_all(); 
    }
}


/**
 * @brief 【API 5】清空指定 PCID 的所有普通页 (保留全局页)
 * @param pcid 目标进程的 PCID
 * @note  常用于杀死后台进程时，清理其残留在 TLB 中的数据
 */
static inline void tlb_flush_pcid_all(uint16 pcid) {
    if (g_cpu_has_invpcid) {
        // Type 1: 灭门指定 PCID，且不动系统的全局页
        asm_invpcid(1, pcid, 0);
    } else {
        // 优雅降级：老 CPU 根本没有 PCID 的概念，
        // 我们只能委屈一下，把当前的整个非全局 TLB 洗掉
        __hw_flush_cr3();
    }
}

/**
 * @brief 【API 6】核武器：毁天灭地全局刷新！
 * @note  清空所有 PCID，并且连带着把内核的 全局页 (Global Pages) 一起抹除。
 *        仅用于：内核页表发生结构性大修 (如 kpage_table_init 切换阶段)
 */
static inline void tlb_flush_global(void) {
    if (g_cpu_has_invpcid) {
        // Type 2: 无视 PCID 边界，无视 G 标志位，连根拔起！
        asm_invpcid(2, 0, 0);
    } else {
        // 老硬件的绝招：先关再开 CR4.PGE (Page Global Enable)
        __hw_toggle_cr4_pge();
    }
}