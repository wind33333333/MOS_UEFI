#pragma once

#include "moslib.h"
#include "acpi.h"

typedef void VOID;
typedef UINT64 UINTN;
typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;
typedef UINT64 EFI_STATUS;
typedef VOID *EFI_HANDLE;

/* UEFI 内存类型定义 (UEFI Specification 2.9) */
#define EFI_RESERVED_MEMORY_TYPE          0  // 预留 - 不可使用
#define EFI_LOADER_CODE                   1  // 引导加载程序代码
#define EFI_LOADER_DATA                   2  // 引导加载程序数据
#define EFI_BOOT_SERVICES_CODE            3  // UEFI 引导服务代码
#define EFI_BOOT_SERVICES_DATA            4  // UEFI 引导服务数据
#define EFI_RUNTIME_SERVICES_CODE         5  // UEFI 运行时服务代码
#define EFI_RUNTIME_SERVICES_DATA         6  // UEFI 运行时服务数据
#define EFI_CONVENTIONAL_MEMORY           7  // 常规可用内存 (操作系统可用)
#define EFI_UNUSABLE_MEMORY               8  // 损坏或不可用内存
#define EFI_ACPI_RECLAIM_MEMORY           9  // ACPI 表内存 (OS启动后可回收)
#define EFI_ACPI_MEMORY_NVS               10 // ACPI NVS 内存 (操作系统不能使用)
#define EFI_MEMORY_MAPPED_IO              11 // 内存映射 I/O (MMIO) 区域
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE   12 // 内存映射 I/O 端口空间
#define EFI_PAL_CODE                      13 // 处理器抽象层代码
#define EFI_PERSISTENT_MEMORY             14 // 持久性内存
#define EFI_MAXMEMORYTYPE                 15  // 内存类型计数上限标志（不是实际类型）

#define IN
#define OUT
#define EFIAPI __attribute__((ms_abi))
#define OPTIONAL
//#define NULL ((VOID *) 0)

typedef enum {
  /// Not used.
  EfiReservedMemoryType,
  /// The code portions of a loaded application.
  /// (Note that UEFI OS loaders are UEFI applications.)
  EfiLoaderCode,
  /// The data portions of a loaded application and the default data allocation
  /// type used by an application to allocate pool memory.
  EfiLoaderData,
  /// The code portions of a loaded Boot Services Driver.
  EfiBootServicesCode,
  /// The data portions of a loaded Boot Serves Driver, and the default data
  /// allocation type used by a Boot Services Driver to allocate pool memory.
  EfiBootServicesData,
  /// The code portions of a loaded Runtime Services Driver.
  EfiRuntimeServicesCode,
  /// The data portions of a loaded Runtime Services Driver and the default
  /// data allocation type used by a Runtime Services Driver to allocate pool memory.
  EfiRuntimeServicesData,
  /// Free (unallocated) memory.
  EfiConventionalMemory,
  /// Memory in which errors have been detected.
  EfiUnusableMemory,
  /// Memory that holds the ACPI tables.
  EfiACPIReclaimMemory,
  /// Address space reserved for use by the firmware.
  EfiACPIMemoryNVS,
  /// Used by system firmware to request that a memory-mapped IO region
  /// be mapped by the OS to a virtual address so it can be accessed by EFI runtime services.
  EfiMemoryMappedIO,
  /// System memory-mapped IO region that is used to translate memory
  /// cycles to IO cycles by the processor.
  EfiMemoryMappedIOPortSpace,
  /// Address space reserved by the firmware for code that is part of the processor.
  EfiPalCode,
  /// A memory region that operates as EfiConventionalMemory,
  /// however it happens to also support byte-addressable non-volatility.
  EfiPersistentMemory,
  EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
  UINT16    Year;
  UINT8     Month;
  UINT8     Day;
  UINT8     Hour; 
  UINT8     Minute;
  UINT8     Second;
  UINT8     Pad1;
  UINT32    Nanosecond;
  INT16     TimeZone;
  UINT8     Daylight;
  UINT8     Pad2;
} EFI_TIME;

typedef struct {
  ///
  /// Provides the reporting resolution of the real-time clock device in
  /// counts per second. For a normal PC-AT CMOS RTC device, this
  /// value would be 1 Hz, or 1, to indicate that the device only reports
  /// the time to the resolution of 1 second.
  ///
  UINT32     Resolution;
  ///
  /// Provides the timekeeping accuracy of the real-time clock in an
  /// error rate of 1E-6 parts per million. For a clock with an accuracy
  /// of 50 parts per million, the value in this field would be
  /// 50,000,000.
  ///
  UINT32     Accuracy;
  ///
  /// A TRUE indicates that a time set operation clears the device's
  /// time below the Resolution reporting level. A FALSE
  /// indicates that the state below the Resolution level of the
  /// device is not cleared when the time is set. Normal PC-AT CMOS
  /// RTC devices set this value to FALSE.
  ///
  BOOLEAN    SetsToZero;
} EFI_TIME_CAPABILITIES;


typedef
EFI_STATUS
(EFIAPI *EFI_GET_TIME)(
  OUT  EFI_TIME                    *Time,
  OUT  EFI_TIME_CAPABILITIES       *Capabilities OPTIONAL
  );

typedef struct
{
    UINT32  Type;
    UINT32  ReservedA;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS  VirtualStart;
    UINT64  NumberOfPages;
    UINT64  Attribute;
    UINT64  ReservedB;
} EFI_MEMORY_DESCRIPTOR;

///
/// Data structure that precedes all of the standard EFI table types.
///
typedef struct {
  ///
  /// A 64-bit signature that identifies the type of table that follows.
  /// Unique signatures have been generated for the EFI System Table,
  /// the EFI Boot Services Table, and the EFI Runtime Services Table.
  ///
  UINT64    Signature;
  ///
  /// The revision of the EFI Specification to which this table
  /// conforms. The upper 16 bits of this field contain the major
  /// revision value, and the lower 16 bits contain the minor revision
  /// value. The minor revision values are limited to the range of 00..99.
  ///
  UINT32    Revision;
  ///
  /// The size, in bytes, of the entire table including the EFI_TABLE_HEADER.
  ///
  UINT32    HeaderSize;
  ///
  /// The 32-bit CRC for the entire table. This value is computed by
  /// setting this field to 0, and computing the 32-bit CRC for HeaderSize bytes.
  ///
  UINT32    CRC32;
  ///
  /// Reserved field that must be set to 0.
  ///
  UINT32    Reserved;
} EFI_TABLE_HEADER;


///
/// This provides the capabilities of the
/// real time clock device as exposed through the EFI interfaces.
///

typedef struct {
  UINT32    Data1;
  UINT16    Data2;
  UINT16    Data3;
  UINT8     Data4[8];
} GUID;

typedef GUID EFI_GUID;

/**
  Returns the current time and date information, and the time-keeping capabilities
  of the hardware platform.

  @param[out]  Time             A pointer to storage to receive a snapshot of the current time.
  @param[out]  Capabilities     An optional pointer to a buffer to receive the real time clock
                                device's capabilities.

  @retval EFI_SUCCESS           The operation completed successfully.
  @retval EFI_INVALID_PARAMETER Time is NULL.
  @retval EFI_DEVICE_ERROR      The time could not be retrieved due to hardware error.
  @retval EFI_UNSUPPORTED       This call is not supported by this platform at the time the call is made.
                                The platform should describe this runtime service as unsupported at runtime
                                via an EFI_RT_PROPERTIES_TABLE configuration table.


typedef
EFI_STATUS
(EFIAPI *EFI_GET_TIME)(
  OUT  EFI_TIME                    *Time,
  OUT  EFI_TIME_CAPABILITIES       *Capabilities OPTIONAL
  );**/

/**
  Sets the current local time and date information.

  @param[in]  Time              A pointer to the current time.

  @retval EFI_SUCCESS           The operation completed successfully.
  @retval EFI_INVALID_PARAMETER A time field is out of range.
  @retval EFI_DEVICE_ERROR      The time could not be set due due to hardware error.
  @retval EFI_UNSUPPORTED       This call is not supported by this platform at the time the call is made.
                                The platform should describe this runtime service as unsupported at runtime
                                via an EFI_RT_PROPERTIES_TABLE configuration table.

**/
typedef
EFI_STATUS
(EFIAPI *EFI_SET_TIME)(
  IN  EFI_TIME                     *Time
  );

/**
  Returns the current wakeup alarm clock setting.

  @param[out]  Enabled          Indicates if the alarm is currently enabled or disabled.
  @param[out]  Pending          Indicates if the alarm signal is pending and requires acknowledgement.
  @param[out]  Time             The current alarm setting.

  @retval EFI_SUCCESS           The alarm settings were returned.
  @retval EFI_INVALID_PARAMETER Enabled is NULL.
  @retval EFI_INVALID_PARAMETER Pending is NULL.
  @retval EFI_INVALID_PARAMETER Time is NULL.
  @retval EFI_DEVICE_ERROR      The wakeup time could not be retrieved due to a hardware error.
  @retval EFI_UNSUPPORTED       This call is not supported by this platform at the time the call is made.
                                The platform should describe this runtime service as unsupported at runtime
                                via an EFI_RT_PROPERTIES_TABLE configuration table.

**/
typedef
EFI_STATUS
(EFIAPI *EFI_GET_WAKEUP_TIME)(
  OUT BOOLEAN                     *Enabled,
  OUT BOOLEAN                     *Pending,
  OUT EFI_TIME                    *Time
  );

/**
  Sets the system wakeup alarm clock time.

  @param[in]  Enable            Enable or disable the wakeup alarm.
  @param[in]  Time              If Enable is TRUE, the time to set the wakeup alarm for.
                                If Enable is FALSE, then this parameter is optional, and may be NULL.

  @retval EFI_SUCCESS           If Enable is TRUE, then the wakeup alarm was enabled. If
                                Enable is FALSE, then the wakeup alarm was disabled.
  @retval EFI_INVALID_PARAMETER A time field is out of range.
  @retval EFI_DEVICE_ERROR      The wakeup time could not be set due to a hardware error.
  @retval EFI_UNSUPPORTED       This call is not supported by this platform at the time the call is made.
                                The platform should describe this runtime service as unsupported at runtime
                                via an EFI_RT_PROPERTIES_TABLE configuration table.

**/
typedef
EFI_STATUS
(EFIAPI *EFI_SET_WAKEUP_TIME)(
  IN  BOOLEAN                      Enable,
  IN  EFI_TIME                     *Time   OPTIONAL
  );

/**
  Changes the runtime addressing mode of EFI firmware from physical to virtual.

  @param[in]  MemoryMapSize     The size in bytes of VirtualMap.
  @param[in]  DescriptorSize    The size in bytes of an entry in the VirtualMap.
  @param[in]  DescriptorVersion The version of the structure entries in VirtualMap.
  @param[in]  VirtualMap        An array of memory descriptors which contain new virtual
                                address mapping information for all runtime ranges.

  @retval EFI_SUCCESS           The virtual address map has been applied.
  @retval EFI_UNSUPPORTED       EFI firmware is not at runtime, or the EFI firmware is already in
                                virtual address mapped mode.
  @retval EFI_INVALID_PARAMETER DescriptorSize or DescriptorVersion is invalid.
  @retval EFI_NO_MAPPING        A virtual address was not supplied for a range in the memory
                                map that requires a mapping.
  @retval EFI_NOT_FOUND         A virtual address was supplied for an address that is not found
                                in the memory map.
  @retval EFI_UNSUPPORTED       This call is not supported by this platform at the time the call is made.
                                The platform should describe this runtime service as unsupported at runtime
                                via an EFI_RT_PROPERTIES_TABLE configuration table.

**/
typedef
EFI_STATUS
(EFIAPI *EFI_SET_VIRTUAL_ADDRESS_MAP)(
  IN  UINTN                        MemoryMapSize,
  IN  UINTN                        DescriptorSize,
  IN  UINT32                       DescriptorVersion,
  IN  EFI_MEMORY_DESCRIPTOR        *VirtualMap
  );

//
// ConvertPointer DebugDisposition type.
//
//#define EFI_OPTIONAL_PTR  0x00000001

/**
  Determines the new virtual address that is to be used on subsequent memory accesses.

  @param[in]       DebugDisposition  Supplies type information for the pointer being converted.
  @param[in, out]  Address           A pointer to a pointer that is to be fixed to be the value needed
                                     for the new virtual address mappings being applied.

  @retval EFI_SUCCESS           The pointer pointed to by Address was modified.
  @retval EFI_NOT_FOUND         The pointer pointed to by Address was not found to be part
                                of the current memory map. This is normally fatal.
  @retval EFI_INVALID_PARAMETER Address is NULL.
  @retval EFI_INVALID_PARAMETER *Address is NULL and DebugDisposition does
                                not have the EFI_OPTIONAL_PTR bit set.
  @retval EFI_UNSUPPORTED       This call is not supported by this platform at the time the call is made.
                                The platform should describe this runtime service as unsupported at runtime
                                via an EFI_RT_PROPERTIES_TABLE configuration table.

**/
typedef
EFI_STATUS
(EFIAPI *EFI_CONVERT_POINTER)(
  IN     UINTN                      DebugDisposition,
  IN OUT VOID                       **Address
  );

/**
  Returns the value of a variable.

  @param[in]       VariableName  A Null-terminated string that is the name of the vendor's
                                 variable.
  @param[in]       VendorGuid    A unique identifier for the vendor.
  @param[out]      Attributes    If not NULL, a pointer to the memory location to return the
                                 attributes bitmask for the variable.
  @param[in, out]  DataSize      On input, the size in bytes of the return Data buffer.
                                 On output the size of data returned in Data.
  @param[out]      Data          The buffer to return the contents of the variable. May be NULL
                                 with a zero DataSize in order to determine the size buffer needed.

  @retval EFI_SUCCESS            The function completed successfully.
  @retval EFI_NOT_FOUND          The variable was not found.
  @retval EFI_BUFFER_TOO_SMALL   The DataSize is too small for the result.
  @retval EFI_INVALID_PARAMETER  VariableName is NULL.
  @retval EFI_INVALID_PARAMETER  VendorGuid is NULL.
  @retval EFI_INVALID_PARAMETER  DataSize is NULL.
  @retval EFI_INVALID_PARAMETER  The DataSize is not too small and Data is NULL.
  @retval EFI_DEVICE_ERROR       The variable could not be retrieved due to a hardware error.
  @retval EFI_SECURITY_VIOLATION The variable could not be retrieved due to an authentication failure.
  @retval EFI_UNSUPPORTED        After ExitBootServices() has been called, this return code may be returned
                                 if no variable storage is supported. The platform should describe this
                                 runtime service as unsupported at runtime via an EFI_RT_PROPERTIES_TABLE
                                 configuration table.

**/
typedef
EFI_STATUS
(EFIAPI *EFI_GET_VARIABLE)(
  IN     CHAR16                      *VariableName,
  IN     EFI_GUID                    *VendorGuid,
  OUT    UINT32                      *Attributes     OPTIONAL,
  IN OUT UINTN                       *DataSize,
  OUT    VOID                        *Data           OPTIONAL
  );

/**
  Enumerates the current variable names.

  @param[in, out]  VariableNameSize The size of the VariableName buffer. The size must be large
                                    enough to fit input string supplied in VariableName buffer.
  @param[in, out]  VariableName     On input, supplies the last VariableName that was returned
                                    by GetNextVariableName(). On output, returns the Nullterminated
                                    string of the current variable.
  @param[in, out]  VendorGuid       On input, supplies the last VendorGuid that was returned by
                                    GetNextVariableName(). On output, returns the
                                    VendorGuid of the current variable.

  @retval EFI_SUCCESS           The function completed successfully.
  @retval EFI_NOT_FOUND         The next variable was not found.
  @retval EFI_BUFFER_TOO_SMALL  The VariableNameSize is too small for the result.
                                VariableNameSize has been updated with the size needed to complete the request.
  @retval EFI_INVALID_PARAMETER VariableNameSize is NULL.
  @retval EFI_INVALID_PARAMETER VariableName is NULL.
  @retval EFI_INVALID_PARAMETER VendorGuid is NULL.
  @retval EFI_INVALID_PARAMETER The input values of VariableName and VendorGuid are not a name and
                                GUID of an existing variable.
  @retval EFI_INVALID_PARAMETER Null-terminator is not found in the first VariableNameSize bytes of
                                the input VariableName buffer.
  @retval EFI_DEVICE_ERROR      The variable could not be retrieved due to a hardware error.
  @retval EFI_UNSUPPORTED       After ExitBootServices() has been called, this return code may be returned
                                if no variable storage is supported. The platform should describe this
                                runtime service as unsupported at runtime via an EFI_RT_PROPERTIES_TABLE
                                configuration table.

**/
typedef
EFI_STATUS
(EFIAPI *EFI_GET_NEXT_VARIABLE_NAME)(
  IN OUT UINTN                    *VariableNameSize,
  IN OUT CHAR16                   *VariableName,
  IN OUT EFI_GUID                 *VendorGuid
  );

/**
  Sets the value of a variable.

  @param[in]  VariableName       A Null-terminated string that is the name of the vendor's variable.
                                 Each VariableName is unique for each VendorGuid. VariableName must
                                 contain 1 or more characters. If VariableName is an empty string,
                                 then EFI_INVALID_PARAMETER is returned.
  @param[in]  VendorGuid         A unique identifier for the vendor.
  @param[in]  Attributes         Attributes bitmask to set for the variable.
  @param[in]  DataSize           The size in bytes of the Data buffer. Unless the EFI_VARIABLE_APPEND_WRITE or
                                 EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS attribute is set, a size of zero
                                 causes the variable to be deleted. When the EFI_VARIABLE_APPEND_WRITE attribute is
                                 set, then a SetVariable() call with a DataSize of zero will not cause any change to
                                 the variable value (the timestamp associated with the variable may be updated however
                                 even if no new data value is provided,see the description of the
                                 EFI_VARIABLE_AUTHENTICATION_2 descriptor below. In this case the DataSize will not
                                 be zero since the EFI_VARIABLE_AUTHENTICATION_2 descriptor will be populated).
  @param[in]  Data               The contents for the variable.

  @retval EFI_SUCCESS            The firmware has successfully stored the variable and its data as
                                 defined by the Attributes.
  @retval EFI_INVALID_PARAMETER  An invalid combination of attribute bits, name, and GUID was supplied, or the
                                 DataSize exceeds the maximum allowed.
  @retval EFI_INVALID_PARAMETER  VariableName is an empty string.
  @retval EFI_OUT_OF_RESOURCES   Not enough storage is available to hold the variable and its data.
  @retval EFI_DEVICE_ERROR       The variable could not be retrieved due to a hardware error.
  @retval EFI_WRITE_PROTECTED    The variable in question is read-only.
  @retval EFI_WRITE_PROTECTED    The variable in question cannot be deleted.
  @retval EFI_SECURITY_VIOLATION The variable could not be written due to EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACESS being set,
                                 but the AuthInfo does NOT pass the validation check carried out by the firmware.

  @retval EFI_NOT_FOUND          The variable trying to be updated or deleted was not found.
  @retval EFI_UNSUPPORTED        This call is not supported by this platform at the time the call is made.
                                 The platform should describe this runtime service as unsupported at runtime
                                 via an EFI_RT_PROPERTIES_TABLE configuration table.

**/
typedef
EFI_STATUS
(EFIAPI *EFI_SET_VARIABLE)(
  IN  CHAR16                       *VariableName,
  IN  EFI_GUID                     *VendorGuid,
  IN  UINT32                       Attributes,
  IN  UINTN                        DataSize,
  IN  VOID                         *Data
  );


/**
  Returns the next high 32 bits of the platform's monotonic counter.

  @param[out]  HighCount        The pointer to returned value.

  @retval EFI_SUCCESS           The next high monotonic count was returned.
  @retval EFI_INVALID_PARAMETER HighCount is NULL.
  @retval EFI_DEVICE_ERROR      The device is not functioning properly.
  @retval EFI_UNSUPPORTED       This call is not supported by this platform at the time the call is made.
                                The platform should describe this runtime service as unsupported at runtime
                                via an EFI_RT_PROPERTIES_TABLE configuration table.

**/
typedef
EFI_STATUS
(EFIAPI *EFI_GET_NEXT_HIGH_MONO_COUNT)(
  OUT UINT32                  *HighCount
  );


///
/// Enumeration of reset types.
///
typedef enum {
  ///
  /// Used to induce a system-wide reset. This sets all circuitry within the
  /// system to its initial state.  This type of reset is asynchronous to system
  /// operation and operates withgout regard to cycle boundaries.  EfiColdReset
  /// is tantamount to a system power cycle.
  ///
  EfiResetCold,
  ///
  /// Used to induce a system-wide initialization. The processors are set to their
  /// initial state, and pending cycles are not corrupted.  If the system does
  /// not support this reset type, then an EfiResetCold must be performed.
  ///
  EfiResetWarm,
  ///
  /// Used to induce an entry into a power state equivalent to the ACPI G2/S5 or G3
  /// state.  If the system does not support this reset type, then when the system
  /// is rebooted, it should exhibit the EfiResetCold attributes.
  ///
  EfiResetShutdown,
  ///
  /// Used to induce a system-wide reset. The exact type of the reset is defined by
  /// the EFI_GUID that follows the Null-terminated Unicode string passed into
  /// ResetData. If the platform does not recognize the EFI_GUID in ResetData the
  /// platform must pick a supported reset type to perform. The platform may
  /// optionally log the parameters from any non-normal reset that occurs.
  ///
  EfiResetPlatformSpecific
} EFI_RESET_TYPE;

/**
  Resets the entire platform.

  @param[in]  ResetType         The type of reset to perform.
  @param[in]  ResetStatus       The status code for the reset.
  @param[in]  DataSize          The size, in bytes, of ResetData.
  @param[in]  ResetData         For a ResetType of EfiResetCold, EfiResetWarm, or
                                EfiResetShutdown the data buffer starts with a Null-terminated
                                string, optionally followed by additional binary data.
                                The string is a description that the caller may use to further
                                indicate the reason for the system reset.
                                For a ResetType of EfiResetPlatformSpecific the data buffer
                                also starts with a Null-terminated string that is followed
                                by an EFI_GUID that describes the specific type of reset to perform.
**/
typedef
VOID
(EFIAPI *EFI_RESET_SYSTEM)(
  IN EFI_RESET_TYPE           ResetType,
  IN EFI_STATUS               ResetStatus,
  IN UINTN                    DataSize,
  IN VOID                     *ResetData OPTIONAL
  );


///
/// EFI Capsule Header.
///
typedef struct {
  ///
  /// A GUID that defines the contents of a capsule.
  ///
  EFI_GUID    CapsuleGuid;
  ///
  /// The size of the capsule header. This may be larger than the size of
  /// the EFI_CAPSULE_HEADER since CapsuleGuid may imply
  /// extended header entries
  ///
  UINT32      HeaderSize;
  ///
  /// Bit-mapped list describing the capsule attributes. The Flag values
  /// of 0x0000 - 0xFFFF are defined by CapsuleGuid. Flag values
  /// of 0x10000 - 0xFFFFFFFF are defined by this specification
  ///
  UINT32      Flags;
  ///
  /// Size in bytes of the capsule (including capsule header).
  ///
  UINT32      CapsuleImageSize;
} EFI_CAPSULE_HEADER;

typedef
EFI_STATUS
(EFIAPI *EFI_UPDATE_CAPSULE)(
  IN EFI_CAPSULE_HEADER     **CapsuleHeaderArray,
  IN UINTN                  CapsuleCount,
  IN EFI_PHYSICAL_ADDRESS   ScatterGatherList   OPTIONAL
  );

typedef
EFI_STATUS
(EFIAPI *EFI_QUERY_CAPSULE_CAPABILITIES)(
  IN  EFI_CAPSULE_HEADER     **CapsuleHeaderArray,
  IN  UINTN                  CapsuleCount,
  OUT UINT64                 *MaximumCapsuleSize,
  OUT EFI_RESET_TYPE         *ResetType
  );

typedef
EFI_STATUS
(EFIAPI *EFI_QUERY_VARIABLE_INFO)(
  IN  UINT32            Attributes,
  OUT UINT64            *MaximumVariableStorageSize,
  OUT UINT64            *RemainingVariableStorageSize,
  OUT UINT64            *MaximumVariableSize
  );
///
/// EFI Runtime Services Table.
///
typedef struct {
  ///
  /// The table header for the EFI Runtime Services Table.
  ///
  EFI_TABLE_HEADER                  Hdr;

  //
  // Time Services
  //
  EFI_GET_TIME                      GetTime;
  EFI_SET_TIME                      SetTime;
  EFI_GET_WAKEUP_TIME               GetWakeupTime;
  EFI_SET_WAKEUP_TIME               SetWakeupTime;

  //
  // Virtual Memory Services
  //
  EFI_SET_VIRTUAL_ADDRESS_MAP       SetVirtualAddressMap;
  EFI_CONVERT_POINTER               ConvertPointer;

  //
  // Variable Services
  //
  EFI_GET_VARIABLE                  GetVariable;
  EFI_GET_NEXT_VARIABLE_NAME        GetNextVariableName;
  EFI_SET_VARIABLE                  SetVariable;

  //
  // Miscellaneous Services
  //
  EFI_GET_NEXT_HIGH_MONO_COUNT      GetNextHighMonotonicCount;
  EFI_RESET_SYSTEM                  ResetSystem;

  //
  // UEFI 2.0 Capsule Services
  //
  EFI_UPDATE_CAPSULE                UpdateCapsule;
  EFI_QUERY_CAPSULE_CAPABILITIES    QueryCapsuleCapabilities;

  //
  // Miscellaneous UEFI 2.0 Service
  //
  EFI_QUERY_VARIABLE_INFO           QueryVariableInfo;
} EFI_RUNTIME_SERVICES;


//region BootInfo结构
typedef struct{
    /*显卡信息*/
    UINT64  frame_buffer_base;
    UINT32  horizontal_resolution;
    UINT32  vertical_resolution;
    UINT32  pixels_per_scan_line;
    UINT64  frame_buffer_size;

    /*内存图*/
    EFI_MEMORY_DESCRIPTOR* mem_map;
    UINT64 mem_descriptor_size;
    UINT64 mem_map_size;
    UINT32 mem_descriptor_version;

    /*RSDP*/
    rsdp_t* rsdp;

    /*UEFI RunTimeServices Point*/
    EFI_RUNTIME_SERVICES* gRTS;

} __attribute__((packed)) boot_info_t;

typedef struct {
    EFI_MEMORY_DESCRIPTOR mem_map[10];
    UINT32 conut;
}efi_runtime_mem_t;

extern  boot_info_t* boot_info;
//endregion

void efi_runtime_service_map(void);
