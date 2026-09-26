#pragma once
#include "moslib.h"

typedef struct {
    uint32   reserved0;
    uint64    rsp0;
    uint64    rsp1;
    uint64    rsp2;
    uint64   reserved1;
    uint64    ist1;
    uint64    ist2;
    uint64    ist3;
    uint64    ist4;
    uint64    ist5;
    uint64    ist6;
    uint64    ist7;
    uint64   reserved2;
    uint16   reserved3;
    uint16   iomap_base;
} __attribute__((packed)) tss_t;

typedef struct {
    uint64 null_desc;          // 0x00: Reserve / Null Descriptor
    uint64 kernel_code64_desc; // 0x08: Ring 0 代码段 (64-bit)
    uint64 kernel_data_desc;   // 0x10: Ring 0 数据段 (通用)
    uint64 user_code32_desc;   // 0x18: Ring 3 代码段 (32-bit 兼容模式)
    uint64 user_data_desc;     // 0x20: Ring 3 数据段 (通用)
    uint64 user_code64_desc;   // 0x28: Ring 3 代码段 (64-bit原生)

    // x86_64 下 TSS 描述符强制占用 16 字节 (两个 8 字节槽位)
    uint64 tss_desc[2];        // 0x30 ~ 0x3F: TSS Descriptor
} __attribute__((packed)) gdt_t;

typedef struct{
    uint16 limit;
    gdt_t *base;
} __attribute__((packed)) gdt_ptr_t;


static inline void asm_lgdt(const gdt_ptr_t *gdt_ptr, uint16 code64_sel, uint16 data64_sel) {
    __asm__ __volatile__(
            "lgdtq       %0                  \n\t"  // 加载 GDT 描述符地址
            "pushq       %q1                 \n\t"  // 压入代码段选择器
            "leaq        1f(%%rip), %%rax    \n\t"  // 获取返回地址
            "pushq       %%rax               \n\t"  // 压入返回地址
            "lretq                           \n\t"  // 执行长返回，切换到新代码段选择子
            "1:                              \n\t"  // 跳转目标标记
            "movw        %2, %%ss            \n\t"  // 设置堆栈段选择器
            "movw        %2, %%ds            \n\t"  // 设置数据段选择器
            "movw        %2, %%es            \n\t"  // 设置额外段选择器
            :
            : "m"(*gdt_ptr), "r"(code64_sel), "r"(data64_sel)
            : "memory", "%rax"
            );
}



static inline void asm_ltr(uint16 tss_sel) {
    __asm__ __volatile__(
            "ltr    %w0 \n\t"
            :
            : "r"(tss_sel)
            :
            );
}


void gdt_tss_init(uint32 logical_id);

