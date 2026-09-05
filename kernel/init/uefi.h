#pragma once

#include "../include/moslib.h"
#include "../include/acpi.h"

typedef void VOID;
typedef uint64 UINTN;
typedef uint64 EFI_PHYSICAL_ADDRESS;
typedef uint64 EFI_VIRTUAL_ADDRESS;
typedef uint64 EFI_STATUS;
typedef VOID *EFI_HANDLE;


// ============================================================================
// UEFI 内存属性掩码 (EFI_MEMORY_DESCRIPTOR -> Attribute 字段)
// 规范来源：UEFI Specification - Section 7.2 Memory Allocation Services
// ============================================================================

// ----------------------------------------------------------------------------
// 【阵营一：硬件缓存策略】 (Cacheability Attributes)
// 决定了这块内存支持 CPU 以何种方式进行缓存。
// 内核在建立页表 (PTE) 时，应根据这些属性设置 PCD, PWT, PAT 等标志位。
// ----------------------------------------------------------------------------
#define EFI_MEMORY_UC   0x0000000000000001ULL // 强无缓存 (Uncacheable)：所有读写直接打到总线，禁止乱序，MMIO 必备。
#define EFI_MEMORY_WC   0x0000000000000002ULL // 合并写 (Write Combining)：不缓存读，但允许 CPU 打包合并写操作，显存必备。
#define EFI_MEMORY_WT   0x0000000000000004ULL // 直写缓存 (Write Through)：读操作走缓存，写操作同时写进缓存和物理内存。
#define EFI_MEMORY_WB   0x0000000000000008ULL // 回写缓存 (Write Back)：最高性能，读写都在缓存中完成，满时再写回，普通 RAM 必备。
#define EFI_MEMORY_UCE  0x0000000000000010ULL // 导出无缓存 (Uncacheable, exported)：支持被外部设备(如 PCIe 设备)窥探的无缓存内存。

// ----------------------------------------------------------------------------
// 【阵营二：内存保护属性】 (Memory Protection Attributes)
// 固件向 OS 声明这块内存的硬件安全能力。
// ----------------------------------------------------------------------------
#define EFI_MEMORY_WP   0x0000000000001000ULL // 写保护 (Write Protected)：硬件层面支持将其配置为不可写。
#define EFI_MEMORY_RP   0x0000000000002000ULL // 读保护 (Read Protected)：硬件层面支持将其配置为不可读。
#define EFI_MEMORY_XP   0x0000000000004000ULL // 执行保护 (Execute Protected)：支持不可执行，对应页表的 NX 位 (No-eXecute)。
#define EFI_MEMORY_RO   0x0000000000020000ULL // 强制只读 (Read Only)：这块内存物理上就是只读的（比如 ROM 芯片），绝对无法写入。

// ----------------------------------------------------------------------------
// 【阵营三：物理与特殊硬件属性】 (Physical & Special Attributes)
// 描述了这块内存在主板上的特殊物理状态。
// ----------------------------------------------------------------------------
#define EFI_MEMORY_NV           0x0000000000008000ULL // 非易失性 (Non-Volatile)：断电数据不丢失，如 SPI Flash、NVRAM。
#define EFI_MEMORY_MORE_RELIABLE 0x0000000000010000ULL // 高可靠性：内存有硬件级别的冗余保护（如内存镜像 RAID），OS 应当把核心数据结构放这里。
#define EFI_MEMORY_SP           0x0000000000040000ULL // 特定用途 (Specific-Purpose)：表示这块内存只能由特定设备使用，不应作为通用 OS 内存分配。
#define EFI_MEMORY_CPU_CRYPTO   0x0000000000080000ULL // 密码保护 (Cryptographically Protected)：支持 CPU 级内存加密（如 AMD SME/SEV, Intel TME）。

// ----------------------------------------------------------------------------
// 【阵营四：生命周期标志】 (Lifecycle Attribute) —— OS 最关键的标志！
// ----------------------------------------------------------------------------
#define EFI_MEMORY_RUNTIME      0x8000000000000000ULL // 运行时留存 (Runtime)：OS 必须为它建立虚拟地址映射，并在 ExitBootServices 后保留它，因为 UEFI 的 Runtime Services 还要用它！

// ----------------------------------------------------------------------------
// 【补充：遗留与架构相关掩码】 (Miscellaneous Masks)
// ----------------------------------------------------------------------------
#define EFI_MEMORY_ISA_VALID    0x4000000000000000ULL // 遗留 ISA 有效：表明下面的 ISA_MASK 包含有效的 ISA 地址。
#define EFI_MEMORY_ISA_MASK     0x0FFFF00000000000ULL // ISA 地址掩码：用于兼容古老的 16 位 ISA 总线设备的内存范围。

#define IN
#define OUT
#define EFIAPI __attribute__((ms_abi))
#define OPTIONAL
//#define NULL ((VOID *) 0)

typedef enum:uint32 {
  /* UEFI 内存类型定义 (UEFI Specification 2.9) */
 EFI_RESERVED_MEMORY_TYPE         ,  // 预留 - 不可使用
 EFI_LOADER_CODE                  ,  // 引导加载程序代码
 EFI_LOADER_DATA                  ,  // 引导加载程序数据
 EFI_BOOT_SERVICES_CODE           ,  // UEFI 引导服务代码
 EFI_BOOT_SERVICES_DATA           , // UEFI 引导服务数据
 EFI_RUNTIME_SERVICES_CODE        ,  // UEFI 运行时服务代码
 EFI_RUNTIME_SERVICES_DATA        ,  // UEFI 运行时服务数据
 EFI_CONVENTIONAL_MEMORY          ,  // 常规可用内存 (操作系统可用)
 EFI_UNUSABLE_MEMORY              ,  // 损坏或不可用内存
 EFI_ACPI_RECLAIM_MEMORY          ,  // ACPI 表内存 (OS启动后可回收)
 EFI_ACPI_MEMORY_NVS              , // ACPI NVS 内存 (操作系统不能使用)
 EFI_MEMORY_MAPPED_IO             , // 内存映射 I/O (MMIO) 区域
 EFI_MEMORY_MAPPED_IO_PORT_SPACE  , // 内存映射 I/O 端口空间
 EFI_PAL_CODE                     , // 处理器抽象层代码
 EFI_PERSISTENT_MEMORY            , // 持久性内存
 EFI_MAXMEMORYTYPE                ,  // 内存类型计数上限标志（不是实际类型）
} EFI_MEMORY_TYPE;

typedef struct {
  uint16    Year;       // 年份（1900 - 9999）
  uint8     Month;      // 月份（1 - 12）
  uint8     Day;        // 日期（1 - 31）
  uint8     Hour;       // 小时（0 - 23）
  uint8     Minute;     // 分钟（0 - 59）
  uint8     Second;     // 秒数（0 - 59）
  uint8     Pad1;       // 填充字节 1（用于保持内存结构的字节对齐）
  uint32    Nanosecond; // 纳秒（0 - 999,999,999）
  int16     TimeZone;   // 时区（以分钟为单位的本地时间与 UTC 的偏移量。范围：-1440 到 1440。特殊值 2047 意味着未指定时区）
  uint8     Daylight;   // 夏令时/日光节约时间标志位掩码（例如：是否处于夏令时，是否受夏令时调整）
  uint8     Pad2;       // 填充字节 2（用于保持内存结构的字节对齐）
} EFI_TIME;

typedef struct {
   /*
   提供实时时钟 (RTC) 设备的报告分辨率，单位为“次/秒 (Hz)”。
   对于一台普通的 PC-AT CMOS RTC 设备，这个值通常是 1 Hz（即值为 1），
   表示该设备只能以 1 秒为最小精度来报告时间。
   */
  uint32     Resolution;


   /*
   提供实时时钟的计时精度（误差率），其单位是百万分之一 (ppm) 的百万分之一 (即 1E-6 ppm)。
   例如：如果一个主板硬件时钟的精度误差是 50 ppm，
   那么这个字段填入的值将会是 50,000,000（即 50 / 1E-6）。
   */
  uint32     Accuracy;

   /*
   值为 TRUE 表示：当操作系统向硬件写入/设置新时间时，
   硬件会自动把低于 Resolution（分辨率）级别的剩余时间（比如亚秒/毫秒状态）清零。
   值为 FALSE 表示：设置新时间时，低于分辨率级别的时间状态不会被清零。
   普通的 PC-AT CMOS RTC 设备通常会将此值设为 FALSE。
   */
  boolean    SetsToZero;
} EFI_TIME_CAPABILITIES;


typedef
EFI_STATUS
(EFIAPI *EFI_GET_TIME)(
  OUT  EFI_TIME                    *Time,
  OUT  EFI_TIME_CAPABILITIES       *Capabilities OPTIONAL
  );

typedef struct
{
    EFI_MEMORY_TYPE  Type;
    uint32  ReservedA;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS  VirtualStart;
    uint64  NumberOfPages;
    uint64  Attribute;
    uint64  ReservedB;
} EFI_MEMORY_DESCRIPTOR;


/// 置于所有标准 EFI 表类型最前面的公共数据结构（即标准表头）。
typedef struct {

   /*
   一个 64 位的签名（魔数），用于标识紧随其后的表的类型。
   UEFI 规范已经为 EFI 系统表 (System Table)、EFI 启动服务表 (Boot Services Table)
   以及 EFI 运行时服务表 (Runtime Services Table) 定义了各自唯一的签名。
   */
  uint64    Signature;

   /*
   该表所遵循的 EFI 规范的版本号。
   此字段的高 16 位包含主版本号 (Major Revision)，
   低 16 位包含次版本号 (Minor Revision)。
   次版本号的取值范围被限制在 00 到 99 之间。
   */
  uint32    Revision;

  uint32    HeaderSize;  /// 整个表的大小（以字节为单位），注意：这个大小是包含当前 EFI_TABLE_HEADER 本身在内的。

   /*
   整个表的 32 位 CRC 校验和。
   计算此值的规则是：在计算之前，必须先将当前这个 CRC32 字段临时清零（设为 0），
   然后对长度为 HeaderSize 字节的整张表进行 32 位 CRC 运算，最后将结果填回此字段。
   */
  uint32    CRC32;

  uint32    Reserved;  /// 保留字段，必须被设置为 0。
} EFI_TABLE_HEADER;


///
/// This provides the capabilities of the
/// real time clock device as exposed through the EFI interfaces.
///

typedef struct {
  uint32    Data1;
  uint16    Data2;
  uint16    Data3;
  uint8     Data4[8];
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
  OUT boolean                     *Enabled,
  OUT boolean                     *Pending,
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
  IN  boolean                      Enable,
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
  IN  uint32                       DescriptorVersion,
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
  IN     char16                      *VariableName,
  IN     EFI_GUID                    *VendorGuid,
  OUT    uint32                      *Attributes     OPTIONAL,
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
  IN OUT char16                   *VariableName,
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
  IN  char16                       *VariableName,
  IN  EFI_GUID                     *VendorGuid,
  IN  uint32                       Attributes,
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
  OUT uint32                  *HighCount
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
  uint32      HeaderSize;
  ///
  /// Bit-mapped list describing the capsule attributes. The Flag values
  /// of 0x0000 - 0xFFFF are defined by CapsuleGuid. Flag values
  /// of 0x10000 - 0xFFFFFFFF are defined by this specification
  ///
  uint32      Flags;
  ///
  /// Size in bytes of the capsule (including capsule header).
  ///
  uint32      CapsuleImageSize;
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
  OUT uint64                 *MaximumCapsuleSize,
  OUT EFI_RESET_TYPE         *ResetType
  );

typedef
EFI_STATUS
(EFIAPI *EFI_QUERY_VARIABLE_INFO)(
  IN  uint32            Attributes,
  OUT uint64            *MaximumVariableStorageSize,
  OUT uint64            *RemainingVariableStorageSize,
  OUT uint64            *MaximumVariableSize
  );

///
/// EFI 运行时服务表 (EFI Runtime Services Table)
/// 注：与 Boot Services (启动服务) 不同，Runtime Services 在操作系统
/// 完全接管硬件（调用 ExitBootServices）后依然可以被 OS 调用！
///
typedef struct {
  ///
  /// 表头信息：包含表签名 ("RTSTVAL")、版本号、头部大小和 CRC32 校验和。
  /// OS 在调用前通常会校验其合法性。
  ///
  EFI_TABLE_HEADER                  Hdr;

  // ==========================================================================
  // 时间服务 (Time Services) —— 也就是操作主板上的 RTC 实时时钟芯片
  // ==========================================================================

  /// 获取系统当前的硬件时间、日期、时区以及夏令时(DST)状态。
  EFI_GET_TIME                      GetTime;

  /// 设置系统的硬件时间（修改主板电池维持的时钟）。
  EFI_SET_TIME                      SetTime;

  /// 获取当前设定的 RTC 唤醒闹钟时间（定时开机状态）。
  EFI_GET_WAKEUP_TIME               GetWakeupTime;

  /// 设定 RTC 唤醒闹钟，可以在系统关机后到了指定时间自动将电脑唤醒开机。
  EFI_SET_WAKEUP_TIME               SetWakeupTime;

  // ==========================================================================
  // 虚拟内存服务 (Virtual Memory Services) —— OS 内存接管的核心过渡点！
  // ==========================================================================

  /// 【核心接口】：告知 UEFI 固件，OS 马上要废弃 1:1 物理映射，全面切换到虚拟地址空间了！
  /// OS 必须传入一个内存描述符数组，告诉固件：“你以前在物理地址 A 的数据，现在被我映射到
  /// 了虚拟地址 B”。固件收到后，会把自身内部所有的指针全部进行 Relocate（重定位）修补。
  EFI_SET_VIRTUAL_ADDRESS_MAP       SetVirtualAddressMap;

  /// 仅在 SetVirtualAddressMap 执行期间由固件内部调用，或者由需要重定位指针的 UEFI 组件调用，
  /// 用于将旧的物理指针转换为新的虚拟指针。OS 自身极少主动调用。
  EFI_CONVERT_POINTER               ConvertPointer;

  // ==========================================================================
  // 环境变量服务 (Variable Services) —— 也就是读写主板 NVRAM 存储
  // ==========================================================================

  /// 从非易失性存储（NVRAM）中读取指定名称和 GUID 的 UEFI 变量。
  /// (比如读取 "BootOrder" 获取启动顺序，或读取 Secure Boot 状态)。
  EFI_GET_VARIABLE                  GetVariable;

  /// 用于遍历 NVRAM 中所有的变量。传入当前变量名，它会返回下一个变量名。
  EFI_GET_NEXT_VARIABLE_NAME        GetNextVariableName;

  /// 创建、修改或删除一个 UEFI 环境变量。很多 OS 层的引导修复工具（如 efibootmgr）
  /// 就是通过调用这个接口把你的 OS 写入主板启动项的。
  EFI_SET_VARIABLE                  SetVariable;

  // ==========================================================================
  // 杂项服务 (Miscellaneous Services)
  // ==========================================================================

  /// 获取下一个高位单调递增的计数值，通常用于防重放攻击的密码学操作或生成唯一标识。
  EFI_GET_NEXT_HIGH_MONO_COUNT      GetNextHighMonotonicCount;

  /// 【超高频接口】：控制整个系统重置（关机、重启、休眠等）。
  /// 在你的 OS 里实现 reboot 或 poweroff 命令时，最标准的底层做法就是调用这个函数。
  EFI_RESET_SYSTEM                  ResetSystem;

  // ==========================================================================
  // UEFI 2.0 胶囊服务 (Capsule Services) —— 主板 BIOS 刷新专用
  // ==========================================================================

  /// 向 UEFI 固件传递一个或多个数据“胶囊”。通常用于在 OS 层面下发 BIOS 更新包，
  /// 系统重启后固件会自动解开胶囊完成主板 BIOS 的无缝升级 (Windows Update 刷 BIOS 就靠它)。
  EFI_UPDATE_CAPSULE                UpdateCapsule;

  /// 查询当前主板固件是否支持某种特定的胶囊更新功能或载荷尺寸。
  EFI_QUERY_CAPSULE_CAPABILITIES    QueryCapsuleCapabilities;

  // ==========================================================================
  // UEFI 2.0 杂项服务扩展
  // ==========================================================================

  /// 查询 NVRAM 变量存储区的容量信息。
  /// (获取最大总存储空间、当前剩余可用空间、以及单个变量允许的最大尺寸)。
  EFI_QUERY_VARIABLE_INFO           QueryVariableInfo;

} EFI_RUNTIME_SERVICES;


//region BootInfo结构
typedef struct{
    /*显卡信息*/
    uint64  frame_buffer_base;
    uint32  horizontal_resolution;
    uint32  vertical_resolution;
    uint32  pixels_per_scan_line;
    uint64  frame_buffer_size;

    /*内存图*/
    EFI_MEMORY_DESCRIPTOR* mem_map;
    uint64 mem_descriptor_size;
    uint64 mem_map_size;
    uint32 mem_descriptor_version;

    /*RSDP*/
    rsdp_t* rsdp;

    /*UEFI RunTimeServices Point*/
    EFI_RUNTIME_SERVICES* gRTS;

} __attribute__((packed)) boot_info_t;

typedef struct {
    EFI_MEMORY_DESCRIPTOR mem_map[10];
    uint32 conut;
}efi_runtime_mem_t;

extern  boot_info_t* boot_info;
//endregion

void efi_runtime_service_init(void);
