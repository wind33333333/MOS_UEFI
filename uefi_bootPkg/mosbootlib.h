#ifndef __MOSBOOTLIB__
#define __MOSBOOTLIB__

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DevicePathToText.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Guid/FileInfo.h>
#include <Library/DevicePathLib.h>
#include <Guid/Acpi.h>

#define KERNELSTARTADDR 0x100000

EFI_STATUS EFIAPI keyCountdown (IN UINT32 Times);
EFI_STATUS EFIAPI PrintInput (IN OUT CHAR16* InputBuffer,IN OUT UINT32* InputBufferLength);
EFI_DEVICE_PATH_PROTOCOL* WalkthroughDevicePath(EFI_DEVICE_PATH_PROTOCOL* DevPath, EFI_STATUS (*Callbk)(EFI_DEVICE_PATH_PROTOCOL*));
EFI_STATUS PrintNode(EFI_DEVICE_PATH_PROTOCOL * Node);

typedef struct{
    CHAR8 Signature[4];        // 表的签名，例如 "XSDT" "ACPI" "HPET" "MCFG"
    UINT32 Length;          // 表的总长度，包括头部
    UINT8 Revision;         // 表的修订版本
    UINT8 Checksum;         // 校验和，所有字节的和应为 0
    CHAR8 OemId[6];            // OEM 标识符
    CHAR8 OemTableId[8];       // OEM 表标识符
    UINT32 OemRevision;     // OEM 表修订版本
    UINT32 CreatorId;       // 创建者的标识符
    UINT32 CreatorRevision; // 创建者的修订版本

}__attribute__((packed)) ACPI_TABLE_HEADER;

typedef struct {
    ACPI_TABLE_HEADER Header;         // 标准 ACPI 表头
    UINT32 EventTimerBlockId;       // 定时器块的 ID
    UINT64 Address;     // 定时器寄存器的地址
    UINT8 HpetNumber;               // HPET 的编号
    UINT16 MinimumTick;             // HPET 支持的最小时间间隔
    UINT8 PageProtection;           // 页保护属性
}__attribute__((packed)) HPET_Struct;

typedef struct {
    ACPI_TABLE_HEADER Header;        // 标准 ACPI 表头
    UINT32 LocalApicAddress;       // 本地 APIC 的物理地址
    UINT32 Flags;                  // 标志，表示系统支持哪些 APIC 功能
    // 接下来的部分是可变长度的 APIC 条目
    UINT8 ApicStructure[];         // APIC 条目数组
}__attribute__((packed)) MADT_Struct;

typedef struct {
    ACPI_TABLE_HEADER Header;
    MADT_Struct* TablePointers[];        // 指向其他 ACPI 表的 64 位指针数组
} __attribute__((packed)) XSDT_Struct;

typedef struct {
    CHAR8 Signature[8];       // "RSD PTR "
    UINT8 Checksum;           // 校验和
    CHAR8 OemId[6];           // OEM 标识符
    UINT8 Revision;           // ACPI 版本号
    UINT32 RsdtAddress;       // RSDT 表（32 位地址）

    // ACPI 2.0 及以后版本的扩展
    UINT32 Length;            // 整个 RSDP 结构的长度
    XSDT_Struct *XsdtAddress;  // XSDT 表（64 位地址）
    UINT8 ExtendedChecksum;   // 扩展校验和，覆盖整个 RSDP 结构
    UINT8 Reserved[3];        // 保留字段，必须为 0
} __attribute__((packed)) RSDP_Struct;

typedef struct{
    /*显卡信息*/
    UINT64  FrameBufferBase;
    UINT32  HorizontalResolution;
    UINT32  VerticalResolution;
    UINT64  FrameBufferSize;

    /*内存图*/
    EFI_MEMORY_DESCRIPTOR* MemMap;
    UINT64 MemDescriptorSize;
    UINT64 MemMapSize;

    /*RSDP*/
    RSDP_Struct* RSDP;

    /*UEFI RunTimeServices Point*/
    EFI_RUNTIME_SERVICES* gRTS;

} __attribute__((packed)) BootInfo_t;


#endif