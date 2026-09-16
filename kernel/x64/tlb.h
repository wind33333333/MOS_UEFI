#pragma once
#include "moslib.h"
#include "alternative.h"

// 刷新类型枚举
#define INVPCID_TYPE_INDIV_ADDR          0  // Type 0: 单页
#define INVPCID_TYPE_SINGLE_CTXT         1  // Type 1: 单一 PCID
#define INVPCID_TYPE_ALL_INCL_GLOBAL     2  // Type 2: 包含全局页的所有 TLB
#define INVPCID_TYPE_ALL_NON_GLOBAL      3  // Type 3: 不含全局页的所有 TLB

// 强制 16 字节对齐的内存描述符
typedef struct {
    uint64 pcid_resv; // 低 12 位是 PCID，高 52 位必须为 0
    uint64 addr;      // 需要刷新的虚拟地址
} __attribute__((aligned(16))) invpcid_desc_t;


/* ========================================================================== */
/* 1. 刷新单个 PCID 标记的单个虚拟页 (Type 0)                                   */
/* ========================================================================== */
static inline void tlb_flush_pcid_page(uint64 pcid, uint64 va) {
    invpcid_desc_t desc = { .pcid_resv = pcid & 0xFFF, .addr = va };
    uint64 type = INVPCID_TYPE_INDIV_ADDR;

    __asm__ __volatile__ (
        ALT_INSTR(
            "invlpg (%[va])",             /* 老指令：降级为传统的单页刷新 */
            "invpcid %[desc], %[type]",   /* 新指令：精准单页狙击 */
            X86_FEATURE_INVPCID
        )
        :
        : [desc] "m" (desc), [type] "r" (type), [va] "r" (va)
        : "memory"
    );
}

/* ========================================================================== */
/* 2. 刷新同一个 PCID 标记的全部普通页 (Type 1)                                 */
/* ========================================================================== */
static inline void tlb_flush_pcid_all(uint64 pcid) {
    invpcid_desc_t desc = { .pcid_resv = pcid & 0xFFF, .addr = 0 };
    uint64 type = INVPCID_TYPE_SINGLE_CTXT;
    uint64 dummy;

    __asm__ __volatile__ (
        ALT_INSTR(
            "mov %%cr3, %[dummy]\n\t"     /* 老指令：降级为重载当前 CR3 */
            "mov %[dummy], %%cr3",
            "invpcid %[desc], %[type]",   /* 新指令：灭门单一 PCID */
            X86_FEATURE_INVPCID
        )
        : [dummy] "=&r" (dummy)
        : [desc] "m" (desc), [type] "r" (type)
        : "memory"
    );
}

/* ========================================================================== */
/* 3. 刷新系统所有非全局页 (Type 3)                                            */
/* ========================================================================== */
static inline void tlb_flush_nonglobals_all(void) {
    invpcid_desc_t desc = {0, 0};
    uint64 type = INVPCID_TYPE_ALL_NON_GLOBAL;
    uint64 dummy;

    __asm__ __volatile__ (
        ALT_INSTR(
            "mov %%cr3, %[dummy]\n\t"
            "mov %[dummy], %%cr3",
            "invpcid %[desc], %[type]",
            X86_FEATURE_INVPCID
        )
        : [dummy] "=&r" (dummy)
        : [desc] "m" (desc), [type] "r" (type)
        : "memory"
    );
}

/* ========================================================================== */
/* 4. 刷新系统全部所有页，包含全局页 (Type 2)                                   */
/* ========================================================================== */
static inline void tlb_flush_all(void) {
    invpcid_desc_t desc = {0, 0};
    uint64 type = INVPCID_TYPE_ALL_INCL_GLOBAL;
    uint64 dummy;

    __asm__ __volatile__ (
        ALT_INSTR(
            "mov %%cr4, %[dummy]\n\t"
            "and $0xFFFFFFFFFFFFFF7FUL, %[dummy]\n\t" /* 清除 CR4.PGE (位7) */
            "mov %[dummy], %%cr4\n\t"
            "or $0x80, %[dummy]\n\t"                  /* 恢复 CR4.PGE (位7) */
            "mov %[dummy], %%cr4",

            "invpcid %[desc], %[type]",
            X86_FEATURE_INVPCID
        )
        : [dummy] "=&r" (dummy)
        : [desc] "m" (desc), [type] "r" (type)
        : "memory"
    );
}

/* ========================================================================== */
/* 5. 传统指令：直接刷新当前上下文的单个虚拟页                                     */
/* ========================================================================== */
static inline void tlb_flush_page(uint64 va) {
    // 工业级规范约束写法：告诉编译器 va 是个内存指针，防止乱序
    __asm__ __volatile__("invlpg %0" : : "m" (*(const char *)va) : "memory");
}