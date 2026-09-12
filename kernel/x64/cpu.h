#pragma once
#include "moslib.h"

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

extern gdt_ptr_t gdt_ptr;

// 1. 基础段类型 (统一，没必要区分32/64)
#define TYPE_CODE           (0xAUL << 40)  // 可执行、可读代码段
#define TYPE_DATA           (0x2UL << 40)  // 可读、可写数据段

// 2. 基础属性
#define S                   (1UL << 44)    // 1=非系统段 (普通代码/数据段)
#define DPL_0               (0UL << 45)    // 特权级 Ring 0
#define DPL_3               (3UL << 45)    // 特权级 Ring 3
#define P                   (1UL << 47)    // Present (存在位)
#define L                   (1UL << 53)    // 64位代码段标志 (Long Mode)
#define DB                  (1UL << 54)    // 32位操作数/堆栈标志 (Default Size)
#define G                   (1UL << 55)    // 4KB 粒度 (Granularity)
#define LIMIT_4G            (0xFUL << 48 | 0xFFFF) // 配合G位表示 4GB 限制

// 3. 终极组合 (5 个核心段)
// =====================================================================
// 内核态 (Ring 0)
#define KERNEL_CODE64_DESC  (TYPE_CODE | DPL_0 | S | P | L)
#define KERNEL_CODE32_DESC  (TYPE_CODE | DPL_0 | S | P | LIMIT_4G | DB | G)
#define KERNEL_DATA_DESC    (TYPE_DATA | DPL_0 | S | P | LIMIT_4G | DB | G)

// 用户态 (Ring 3) - 用户态需要兼容 32位 和 64位
#define USER_CODE64_DESC    (TYPE_CODE | DPL_3 | S | P | L)
#define USER_CODE32_DESC    (TYPE_CODE | DPL_3 | S | P | LIMIT_4G | DB | G)
#define USER_DATA_DESC      (TYPE_DATA | DPL_3 | S | P | LIMIT_4G | DB | G) // 32位和64位通用！



typedef struct {
    uint32   reserved0;
    uint64   rsp0;
    uint64   rsp1;
    uint64   rsp2;
    uint64   reserved1;
    uint64   ist1;
    uint64   ist2;
    uint64   ist3;
    uint64   ist4;
    uint64   ist5;
    uint64   ist6;
    uint64   ist7;
    uint64   reserved2;
    uint16   reserved3;
    uint16   iomap_base;
} __attribute__((packed)) tss_t;



uint32 apicid_to_cpuid(uint32 apic_id);
uint32 cpuid_to_apicid(uint32 cpu_id);
void bsp_init(void);
void cpu_feature_init(void);

typedef struct {
    char8 manufacturer_name[13];
    char8 model_name[49];
    uint32 logical_processors_number;
    uint32 fundamental_hz;
    uint32 maximum_hz;
    uint32 bus_hz;
    uint32 tsc_hz;
}cpu_info_t;

extern cpu_info_t cpu_info;
extern uint32 *apic_id_table;

void ap_init(void);
void ap_main(void);

typedef struct {
    uint64 rsp;
    uint16 tr;
}__attribute__((packed)) apboot_data_t;

extern uint8 _apboot_start[];
extern uint8 _apboot_end[];

extern uint64 ap_rsp_ptr;
extern void *ap_main_ptr;
extern uint64* ap_tmp_pml4t_ptr;
extern uint32 *apic_id_table_ptr;
extern uint64 ap_boot_loader_address;
