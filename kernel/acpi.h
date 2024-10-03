#ifndef __acpi_init__
#define __acpi_init__
#include "lib.h"
#include "printk.h"
#include "cpu.h"
#include "ioapic.h"
#include "hpet.h"

void acpi_init(unsigned char bsp_flags);

typedef struct {
    unsigned int IRQ;
    unsigned int GSI;
}IRQTOGSI;

IRQTOGSI irq_to_gsi[24] = {0,0};

// 定义RSDP结构
typedef struct {
    char Signature[8];                   // 必须为 "RSD PTR "
    unsigned char Checksum;              // 检验和，确保整个表的字节和为0
    char OEMID[6];                       // OEM ID
    unsigned char Revision;              // ACPI版本号
    unsigned int RsdtAddress;           // RSDT的物理地址（32位）
    unsigned int Length;                // RSDP结构长度（仅在ACPI 2.0中使用）
    unsigned long XsdtAddress;           // XSDT的物理地址（仅在ACPI 2.0中使用）
    unsigned char ExtendedChecksum;      // 扩展校验和（仅在ACPI 2.0中使用）
    unsigned char Reserved[3];           // 保留，必须为0
} __attribute__((packed)) RSDP;


// 定义RSDT结构
typedef struct {
    char Signature[4];                      // 必须为 "RSDT"
    unsigned int Length;                     // 表的长度，包括表头
    unsigned char Revision;                  // 表的修订版本
    unsigned char Checksum;                 // 检验和
    char OEMID[6];                          // OEM ID
    char OEMTableID[8];                     // OEM表ID
    unsigned int OEMRevision;               // OEM表修订版
    unsigned int CreatorID;                 // 表的创建者ID
    unsigned int CreatorRevision;           // 表的创建者修订版
    unsigned int Entry[];                   // ACPI表指针数组（RSDT为32位指针，XSDT为64位指针）
} __attribute__((packed)) RSDT;

// 定义XSDT结构
typedef struct {
    char Signature[4];                  // 必须为 "XSDT"
    unsigned int Length;                // 表的长度，包括表头
    unsigned char Revision;             // 表的修订版本
    unsigned char Checksum;             // 检验和
    char OEMID[6];                      // OEM ID
    char OEMTableID[8];                 // OEM表ID
    unsigned int OEMRevision;           // OEM表修订版
    unsigned int CreatorID;             // 表的创建者ID
    unsigned int CreatorRevision;       // 表的创建者修订版
    unsigned long Entry[];              // ACPI表指针数组（64位指针）
} __attribute__((packed)) XSDT;

// APIC结构的公共头部
typedef struct {
    unsigned char Type;            // APIC结构的类型
    unsigned char Length;          // APIC结构的长度
}__attribute__((packed)) APICHeader;

//定义IOAPIC结构
typedef struct {
    APICHeader Header;              // APIC结构的公共头部
    unsigned char ioapic_id;
    unsigned char reserved;
    unsigned int ioapic_address;
}__attribute__((packed)) IOAPIC;

// 中断源覆盖结构
typedef struct {
    APICHeader Header;              // APIC结构的公共头部
    unsigned char Bus;
    unsigned char Source;
    unsigned int GlobalSystemInterrupt;
    unsigned short Flags;
}__attribute__((packed)) InterruptSourceOverride;

// 定义MADT结构
typedef struct {
    char Signature[4];                  // 必须为 "APIC"
    unsigned int Length;                // 表的长度，包括表头
    unsigned char Revision;             // 表的修订版本
    unsigned char Checksum;             // 检验和
    char OEMID[6];                      // OEM ID
    char OEMTableID[8];                 // OEM表ID
    unsigned int OEMRevision;           // OEM表修订版
    unsigned int CreatorID;             // 表的创建者ID
    unsigned int CreatorRevision;       // 表的创建者修订版
    unsigned int LocalAPICAddress;      // 本地APIC的物理地址
    unsigned int Flags;                 // 标志
    APICHeader Header[];                // APIC结构的公共头部
} __attribute__((packed)) MADT;


// 定义HPET结构
typedef struct {
    char Signature[4];                      // 必须为 "HPET"
    unsigned int Length;                    // 表的长度，包括表头
    unsigned char Revision;                 // 表的修订版本
    unsigned char Checksum;                 // 校验和
    char OEMID[6];                          // OEM ID
    char OEMTableID[8];                     // OEM表ID
    unsigned int OEMRevision;               // OEM表修订版
    unsigned int CreatorID;                 // 表的创建者ID
    unsigned int CreatorRevision;           // 表的创建者修订版
    unsigned int EventTimerBlockID;         // 事件计时器块ID
    unsigned int BaseAddressLower;          // 基地址低32位
    unsigned int BaseAddressUpper;          // 基地址高32位（可选）
    unsigned char HpetNumber;               // HPET的编号
    unsigned short MinimumClockTick;        // 最小时钟周期，以飞秒为单位
    unsigned char PageProtection;           // 页保护属性
} __attribute__((packed)) HPET;


typedef struct {
    char Signature[4];          // 表签名，应为"MCFG"
    unsigned int Length;            // 表的总长度，包括表头和所有配置空间描述符
    unsigned char Revision;           // 表的修订版本
    unsigned char Checksum;           // 表的校验和
    char OEMID[6];              // OEM ID
    char OEMTableID[8];         // OEM 表 ID
    unsigned int OEMRevision;       // OEM 修订版本
    unsigned int CreatorID;         // 创建者 ID
    unsigned int CreatorRevision;   // 创建者修订版本
    unsigned int Reserved;          // 保留字段，应为0
} MCFGHeader;

typedef struct {
    unsigned long BaseAddress;       // PCIe配置空间的基地址
    unsigned int SegmentGroupNumber;// PCIe段组号
    unsigned char StartBusNumber;     // 起始总线号
    unsigned char EndBusNumber;       // 结束总线号
    unsigned int Reserved;          // 保留字段，应为0
} MCFGConfigurationSpace;

typedef struct {
    MCFGHeader Header;
    MCFGConfigurationSpace ConfigSpaces[];
} __attribute__((packed)) MCFG;


#endif