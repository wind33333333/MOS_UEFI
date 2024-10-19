#ifndef __init_acpi__
#define __init_acpi__
#include "moslib.h"

void init_acpi(UINT8 bsp_flags);

typedef struct {
    UINT32 IRQ;
    UINT32 GSI;
}irq_to_gsi_t;

irq_to_gsi_t irq_to_gsi[24] = {0,0};

typedef struct {
    CHAR8 signature[4];        // 表的签名，例如 "XSDT" "ACPI" "HPET" "MCFG"
    UINT32 length;             // 表的总长度，包括头部
    UINT8 revision;            // 表的修订版本
    UINT8 checksum;            // 校验和，所有字节的和应为 0
    CHAR8 oem_id[6];           // OEM 标识符
    CHAR8 oem_table_id[8];     // OEM 表标识符
    UINT32 oem_revision;       // OEM 表修订版本
    UINT32 creator_id;         // 创建者的标识符
    UINT32 creator_revision;   // 创建者的修订版本
} __attribute__((packed)) acpi_table_header_t;

typedef struct {
    UINT64 base_address;        // 配置空间基地址
    UINT64 pci_segment;         // PCIe 段号
    UINT64 start_bus_number;    // 起始总线号
    UINT64 end_bus_number;      // 结束总线号
    UINT64 reserved;            // 保留字段，通常为 0
} mcfg_entry_t;

typedef struct {
    acpi_table_header_t header;                // 标准 ACPI 表头（36 字节）
    UINT64 reserved;                           // 保留字段（8 字节），应为 0
    mcfg_entry_t config_space_base[];          // PCIe 配置空间的区域描述符列表
} mcfg_t;

typedef struct {
    acpi_table_header_t header;         // 标准 ACPI 表头
    UINT32 event_timer_block_id;        // 定时器块的 ID
    UINT64 address;                     // 定时器寄存器的地址
    UINT8  hpet_number;                 // HPET 的编号
    UINT16 minimum_tick;                // HPET 支持的最小时间间隔
    UINT8  page_protection;             // 页保护属性
}__attribute__((packed)) hpett_t;

typedef struct {
    acpi_table_header_t header;            // 标准 ACPI 表头
    UINT32 local_apic_address;             // 本地 APIC 的物理地址
    UINT32 flags;                          // 标志，表示系统支持哪些 APIC 功能
    // 接下来的部分是可变长度的 APIC 条目
    UINT8 apic_structure[];                // APIC 条目数组
}__attribute__((packed)) madt_t;

typedef struct {
    acpi_table_header_t header;          // 标准 ACPI 表头
    UINT32 *entry[];                     // 指向其他 ACPI 表的 64 位指针数组
} __attribute__((packed)) xsdt_t;

typedef struct {
    CHAR8 signature[8];        // "RSD PTR "
    UINT8 checksum;            // 校验和
    CHAR8 oem_id[6];           // OEM 标识符
    UINT8 revision;            // ACPI 版本号
    UINT32 rsdt_address;       // RSDT 表（32 位地址）

    // ACPI 2.0 及以后版本的扩展
    UINT32 length;               // 整个 RSDP 结构的长度
    xsdt_t* xsdt_address;        // XSDT 表（64 位地址）
    UINT8 extended_checksum;     // 扩展校验和，覆盖整个 RSDP 结构
    UINT8 reserved[3];           // 保留字段，必须为 0
} __attribute__((packed)) rsdp_t;


#endif