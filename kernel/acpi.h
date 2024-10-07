#ifndef __init_acpi__
#define __init_acpi__
#include "moslib.h"
#include "printk.h"
#include "cpu.h"
#include "ioapic.h"
#include "hpet.h"

void init_acpi(UINT8 bsp_flags);

typedef struct {
    UINT32 IRQ;
    UINT32 GSI;
}IRQTOGSI;

IRQTOGSI irq_to_gsi[24] = {0,0};

// 定义RSDP结构
typedef struct {
    char Signature[8];                   // 必须为 "RSD PTR "
    UINT8 Checksum;              // 检验和，确保整个表的字节和为0
    char OEMID[6];                       // OEM ID
    UINT8 Revision;              // ACPI版本号
    UINT32 RsdtAddress;           // RSDT的物理地址（32位）
    UINT32 Length;                // RSDP结构长度（仅在ACPI 2.0中使用）
    UINT64 XsdtAddress;           // XSDT的物理地址（仅在ACPI 2.0中使用）
    UINT8 ExtendedChecksum;      // 扩展校验和（仅在ACPI 2.0中使用）
    UINT8 Reserved[3];           // 保留，必须为0
} __attribute__((packed)) RSDP;


// 定义RSDT结构
typedef struct {
    char Signature[4];                      // 必须为 "RSDT"
    UINT32 Length;                     // 表的长度，包括表头
    UINT8 Revision;                  // 表的修订版本
    UINT8 Checksum;                 // 检验和
    char OEMID[6];                          // OEM ID
    char OEMTableID[8];                     // OEM表ID
    UINT32 OEMRevision;               // OEM表修订版
    UINT32 CreatorID;                 // 表的创建者ID
    UINT32 CreatorRevision;           // 表的创建者修订版
    UINT32 Entry[];                   // ACPI表指针数组（RSDT为32位指针，XSDT为64位指针）
} __attribute__((packed)) RSDT;

// 定义XSDT结构
typedef struct {
    char Signature[4];                  // 必须为 "XSDT"
    UINT32 Length;                // 表的长度，包括表头
    UINT8 Revision;             // 表的修订版本
    UINT8 Checksum;             // 检验和
    char OEMID[6];                      // OEM ID
    char OEMTableID[8];                 // OEM表ID
    UINT32 OEMRevision;           // OEM表修订版
    UINT32 CreatorID;             // 表的创建者ID
    UINT32 CreatorRevision;       // 表的创建者修订版
    UINT64 Entry[];              // ACPI表指针数组（64位指针）
} __attribute__((packed)) XSDT;

// APIC结构的公共头部
typedef struct {
    UINT8 Type;            // APIC结构的类型
    UINT8 Length;          // APIC结构的长度
}__attribute__((packed)) APICHeader;

//定义IOAPIC结构
typedef struct {
    APICHeader Header;              // APIC结构的公共头部
    UINT8 ioapic_id;
    UINT8 reserved;
    UINT32 ioapic_address;
}__attribute__((packed)) IOAPIC;

// 中断源覆盖结构
typedef struct {
    APICHeader Header;              // APIC结构的公共头部
    UINT8 Bus;
    UINT8 Source;
    UINT32 GlobalSystemInterrupt;
    UINT16 Flags;
}__attribute__((packed)) InterruptSourceOverride;

// 定义MADT结构
typedef struct {
    char Signature[4];                  // 必须为 "APIC"
    UINT32 Length;                // 表的长度，包括表头
    UINT8 Revision;             // 表的修订版本
    UINT8 Checksum;             // 检验和
    char OEMID[6];                      // OEM ID
    char OEMTableID[8];                 // OEM表ID
    UINT32 OEMRevision;           // OEM表修订版
    UINT32 CreatorID;             // 表的创建者ID
    UINT32 CreatorRevision;       // 表的创建者修订版
    UINT32 LocalAPICAddress;      // 本地APIC的物理地址
    UINT32 Flags;                 // 标志
    APICHeader Header[];                // APIC结构的公共头部
} __attribute__((packed)) MADT;


// 定义HPET结构
typedef struct {
    char Signature[4];                      // 必须为 "HPET"
    UINT32 Length;                    // 表的长度，包括表头
    UINT8 Revision;                 // 表的修订版本
    UINT8 Checksum;                 // 校验和
    char OEMID[6];                          // OEM ID
    char OEMTableID[8];                     // OEM表ID
    UINT32 OEMRevision;               // OEM表修订版
    UINT32 CreatorID;                 // 表的创建者ID
    UINT32 CreatorRevision;           // 表的创建者修订版
    UINT32 EventTimerBlockID;         // 事件计时器块ID
    UINT32 BaseAddressLower;          // 基地址低32位
    UINT32 BaseAddressUpper;          // 基地址高32位（可选）
    UINT8 hpetNumber;               // HPET的编号
    UINT16 MinimumClockTick;        // 最小时钟周期，以飞秒为单位
    UINT8 PageProtection;           // 页保护属性
} __attribute__((packed)) HPET;


typedef struct {
    char Signature[4];          // 表签名，应为"MCFG"
    UINT32 Length;            // 表的总长度，包括表头和所有配置空间描述符
    UINT8 Revision;           // 表的修订版本
    UINT8 Checksum;           // 表的校验和
    char OEMID[6];              // OEM ID
    char OEMTableID[8];         // OEM 表 ID
    UINT32 OEMRevision;       // OEM 修订版本
    UINT32 CreatorID;         // 创建者 ID
    UINT32 CreatorRevision;   // 创建者修订版本
    UINT32 Reserved;          // 保留字段，应为0
} MCFGHeader;

typedef struct {
    UINT64 BaseAddress;       // PCIe配置空间的基地址
    UINT32 SegmentGroupNumber;// PCIe段组号
    UINT8 StartBusNumber;     // 起始总线号
    UINT8 EndBusNumber;       // 结束总线号
    UINT32 Reserved;          // 保留字段，应为0
} MCFGConfigurationSpace;

typedef struct {
    MCFGHeader Header;
    MCFGConfigurationSpace ConfigSpaces[];
} __attribute__((packed)) MCFG;


#endif