#ifndef __init_acpi__
#define __init_acpi__
#include "moslib.h"

//region acpi通用头
typedef struct {
    UINT32 signature;        // 表的签名，例如 "XSDT" "ACPI" "HPET" "MCFG"
    UINT32 length;             // 表的总长度，包括头部
    UINT8 revision;            // 表的修订版本
    UINT8 checksum;            // 校验和，所有字节的和应为 0
    CHAR8 oem_id[6];           // OEM 标识符
    CHAR8 oem_table_id[8];     // OEM 表标识符
    UINT32 oem_revision;       // OEM 表修订版本
    UINT32 creator_id;         // 创建者的标识符
    UINT32 creator_revision;   // 创建者的修订版本
} __attribute__((packed)) acpi_header_t;
//endregion

//region GAS通用地址结构
typedef struct{
    UINT8  space_id;         // 地址空间类型 0x00=系统内存空间 0x01=I/O端口空间 /0x02=PCI配置空间  /0x03=嵌入式控制器空间（EC）/0x04=SMBus /0x07=CMOS /0x0A=固件控制器（Functional Fixed Hardware）
    UINT8  bit_width;        // 寄存器宽度（以位为单位）
    UINT8  bit_offset;       // 位偏移
    UINT8  access_size;      // 访问大小
    UINT64 address;         // 基地址
}__attribute__((packed)) acpi_generic_adderss_t ;
//endregion

//region mcfg表
typedef struct {
    UINT64              base_address;        // ECAM配置空间基地址
    UINT16              pci_segment;         // PCIe 段号
    UINT8               start_bus;           // 起始总线号
    UINT8               end_bus;             // 结束总线号
    UINT32              reserved;            // 保留字段，通常为 0
}__attribute__((packed)) mcfg_entry_t;

typedef struct {
    acpi_header_t   acpi_header;                 // 标准 ACPI 表头（36 字节）
    UINT64          reserved;                    // 保留字段（8 字节），应为 0
    mcfg_entry_t    entry[];                     // PCIe 配置空间的区域描述符列表
}__attribute__((packed)) mcfg_t;
//endregion

//region hpet表
typedef struct {
    acpi_header_t               acpi_header;                 // 标准 ACPI 表头
    UINT32                      event_timer_block_id;        // 定时器块的 ID
    acpi_generic_adderss_t      acpi_generic_adderss;        // GAS通用地址结构
    UINT8                       hpet_number;                 // HPET 的编号
    UINT16                      minimum_tick;                // HPET 支持的最小时间间隔
    UINT8                       page_protection;             // 页保护属性
}__attribute__((packed)) hpett_t;
//endregion

//region madt表
// 通用madt头
typedef struct{
    UINT8 type;             // 类型
    UINT8 length;           // 长度
}__attribute__((packed)) madt_header_t;

typedef struct {
    madt_header_t                   madt_header;                    // 类型，0 表示本地 APIC条目 长度，通常为 8
    UINT8                           processor_id;                   // ACPI 处理器 ID
    UINT8                           apic_id;                        // 本地 APIC ID
    UINT32                          flags;                          // 启用状态（1 表示启用）
}__attribute__((packed)) apic_entry_t;

typedef struct {
    madt_header_t                   madt_header;                    // 类型，1 表示 IO APIC // 条目长度，通常为 12
    UINT8                           ioapic_id;                      // IO APIC 的 ID
    UINT8                           reserved;                       // 保留，通常为 0
    UINT32                          ioapic_address;                 // IO APIC 的内存映射基地址
    UINT32                          global_system_interrupt_base;   // 全局中断号基址
}__attribute__((packed)) ioapic_entry_t;

typedef struct {
    madt_header_t                   madt_header;                   // 类型，2 表示中断源重定向  条目长度，通常为 10
    UINT8                           bus_source;                    // 总线源（通常为 0，表示 ISA 总线）
    UINT8                           irq_source;                    // ISA IRQ 号（0-15）
    UINT32                          global_system_interrupt;       // 重定向后的全局系统中断号
    UINT16                          flags;                         // 中断标志（边沿/电平触发，高/低电平等）
}__attribute__((packed)) interrupt_source_override_entry_t;

typedef struct {
    madt_header_t                   madt_header;                   // 类型，3 不可屏蔽中断  条目长度，通常为 8 字节
    UINT16                          flags;                         // 触发模式和极性标志
    UINT32                          global_interrupt;              // 全局系统中断号
}__attribute__((packed)) nmi_source_entry_t;

typedef struct {
    madt_header_t                   madt_header;                   // 类型，4 表示 NMI 源  条目长度，通常为 6
    UINT8                           apic_id;                       // ACPI 处理器 ID，0xFF 表示所有处理器
    UINT16                          flags;                         // 标志位，表示触发模式和极性
    UINT8                           lint;                          // LINT 引脚，表示使用 LINT0 或 LINT1
}__attribute__((packed)) apic_nmi_entry_t;

typedef struct {
    madt_header_t                   madt_header;               // 类型（5）长度（12）用于指定 64 位地址的本地 APIC（LAPIC）地址
    UINT16                          reserved;                  // 保留字段（0）
    UINT64                          apic_address;              // 64 位 LAPIC 地址
}__attribute__((packed)) apic_address_override_entry_t;

typedef struct {
    madt_header_t                   madt_header;         // 类型9,条目长度，通常为 16 字节x2APIC 是增强版的 APIC，支持更大范围的 APIC ID
    UINT16                          reserved;            // 保留字段，必须为 0
    UINT32                          x2apic_id;           // x2APIC 的 ID
    UINT32                          flags;               // 标志位，指示处理器是否启用
    UINT32                          processor_id;        // ACPI 中的处理器唯一 ID
}__attribute__((packed)) x2apic_entry_t;

typedef struct {
    madt_header_t                   madt_header;              // 类型10,条目长度，通常为 12 字节 x2APIC 处理器的 NMI（非屏蔽中断）
    UINT16                          flags;                    // 标志位，指示 NMI 的触发模式和极性
    UINT32                          x2apic_id;                // 目标处理器的 ACPI 唯一 ID
    UINT32                          lint;                     // 本地 APIC 的 LINT 输入引脚（LINT0 或 LINT1）
    UINT8                           reserved[3];              // 保留字段，必须为 0
}__attribute__((packed)) x2apic_nmi_entry_t;

typedef struct {
    madt_header_t                   madt_header;              // 类型13 条目长度，通常为 12 字节 处理器系统中 唤醒逻辑
    UINT16                          reserved;                 // 保留字段，必须为 0
    UINT32                          mailbox_address;          // 唤醒邮箱的物理地址
    UINT32                          reserved2;                // 保留字段，必须为 0
} multiprocessor_wakeup_entry_t;

typedef struct {
    acpi_header_t                    acpi_header;                    // 标准 ACPI 表头
    UINT32                           local_apic_address;             // 本地 APIC 的物理地址
    UINT32                           flags;                          // 标志，表示系统支持哪些 APIC 功能
    // 接下来的部分是可变长度的 APIC结构数组
    madt_header_t                    entry[];                        // APIC数组
}__attribute__((packed)) madt_t;
//endregion

//region xsdt表
typedef struct {
    acpi_header_t acpi_header;           // 标准 ACPI 表头
    acpi_header_t *entry[];              // 指向其他 ACPI 表的 64 位指针数组
} __attribute__((packed)) xsdt_t;
//endregion

//region rsdp表
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
//endregion

//region intel DMAR表
//DMAR主表
typedef struct {
    acpi_header_t   acpi_header;                 // 标准 ACPI 表头（36 字节
    /* DMAR特定字段 */
    UINT8            host_address_width;         // 物理地址宽度 = 值 + 1（如39表示40位地址）
    UINT8            flags;                      // 标志位：
    //   Bit 0: INTR_REMAP - 中断重映射支持
    //   Bit 1: X2APIC_OPT_OUT - X2APIC模式禁用
    //   Bit 2: DMA_CTRL_PLATFORM - 平台DMA控制
    UINT8            reserved[10];               // 保留字段（必须为0）
    // 后面跟随子表（DRHD, RMRR, etc.)
} __attribute__((packed)) dmar_t;

// Device Scope（DRHD/RMRR 中）
typedef struct {
    UINT8       type;           // 0x01 (PCI Endpoint), 0x02 (Sub-hierarchy), etc.
    UINT8       length;
    UINT16      reserved;
    UINT8       enumeration_id;
    UINT8       bus;
    UINT8       device;
    UINT8       function;
    // 可变长度 Path 字段
} __attribute__((packed)) dmar_device_scope_t;

// DRHD 子表
typedef struct {
    UINT16                  type;          // 0x00
    UINT16                  length;
    UINT8                   flags;          // 位 0: INCLUDE_PCI_ALL
    UINT8                   reserved;
    UINT16                  segment;       // PCIe 段号
    UINT64                  address;       // IOMMU 寄存器基地址
    // 后面跟随 dmar_device_scope_t
} __attribute__((packed)) dmar_drhd_t;

// RMRR 子表（简化）
struct {
    UINT16      type;          // 0x01
    UINT16      length;
    UINT16      reserved;
    UINT16      segment;
    UINT64      base_address;  // 保留内存区域起始地址
    UINT64      end_address;   // 保留内存区域结束地址
    // 后面跟随 dmar_device_scope_t
} __attribute__((packed)) dmar_rmrr_t;

// ATSR 子表
typedef struct {
    UINT16      type;          // 0x02
    UINT16      length;        // 子表长度
    UINT8       flags;          // 位 0: ALL_PORTS
    UINT8       reserved;       // 0
    UINT16      segment;       // PCIe 段号
    // 后面跟随 Device Scope 数组
} __attribute__((packed)) dmar_atsr_t;

// RHSA 子表
typedef struct {
    UINT16      type;          // 0x03
    UINT16      length;        // 16
    UINT32      reserved;      // 0
    UINT32      proximity_domain; // NUMA 节点 ID
    UINT64      reg_base_addr; // IOMMU 寄存器基地址（匹配 DRHD 的 address）
} __attribute__((packed)) dmar_rhsa_t;

// ANDD 子表
typedef struct {
    UINT16       type;                // 0x04
    UINT16       length;              // 子表长度
    UINT8        reserved[3];         // 0
    UINT8        device_name_length;  // 设备名称长度（包括 \0）
    char         device_name[];       // 可变长度，NUL 终止的 ACPI 设备名称
} __attribute__((packed)) dmar_andd_t;

//endregion

//region amd IVRS表
// IVRS主表
typedef struct {
    acpi_header_t   header;
    UINT32          info;          // 功能信息
    UINT32          reserved;
    UINT32          ivhd_offset;   // 第一个 IVHD 的偏移
    // 后面跟随子表（IVHD, IVMD）
} __attribute__((packed)) ivrs_t;

// IVHD 子表（类型 0x10）
typedef struct {
    UINT8       type;           // 0x10
    UINT8       flags;          // 功能标志
    UINT16      length;        // 子表长度
    UINT16      device_id;     // IOMMU 设备的 BDF
    UINT16      capability_offset; // 能力寄存器偏移
    UINT64      iommu_base;    // IOMMU 寄存器基地址
    UINT16      segment;       // PCIe 段号
    UINT16      reserved;      // 保留
    // 后面跟随 Device Entries
} __attribute__((packed)) ivrs_ivhd_t;

// IVHD 子表（类型 0x11，扩展）
typedef struct {
    UINT8       type;           // 0x11
    UINT8       flags;
    UINT16      length;
    UINT16      device_id;
    UINT16      capability_offset;
    UINT64      iommu_base;
    UINT16      segment;
    UINT32      info;          // 扩展功能信息
    UINT32      efr;           // Extended Feature Register
    // 后面跟随 Device Entries
} __attribute__((packed)) ivrs_ivhd_ext_t;

// Device Entry（简化）
typedef struct {
    UINT8       type;           // 0x01 (Select), 0x02 (Start Range), 0x03 (End Range), etc.
    UINT16      device_id;     // PCIe BDF
    UINT8       additional_data; // 视类型而定
} __attribute__((packed)) ivrs_device_entry;

// IVMD 子表（类型 0x20-0x22）
typedef struct {
    UINT8       type;           // 0x20, 0x21, 0x22
    UINT8       flags;          // 内存访问标志
    UINT16      length;        // 子表长度
    UINT16      device_id;     // 设备 BDF（0x20 为 0）
    UINT16      auxiliary_data; // 辅助数据
    UINT64      reserved;      // 保留
    UINT64      base_address;  // 保留内存起始地址
    UINT64      end_address;   // 保留内存结束地址
} __attribute__((packed)) ivrs_ivmd_t;

//endregion

void *acpi_get_table(UINT32 table);

#endif