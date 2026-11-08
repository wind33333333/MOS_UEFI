#include "mosbootlib.h"

// UEFI 应用程序的入口函数
// ImageHandle: 当前引导程序的镜像句柄，可用于获取加载的设备路径等信息
// SystemTable: UEFI 核心系统表，包含了控制台输入输出、引导服务(gBS)和运行时服务(gRT)的指针
EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable) {

    // CpuBreakpoint();  // 启用调试模式，触发断点异常，供连接的 GDB 或调试器捕获

    EFI_STATUS Status;
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);       // 清空控制台屏幕
    SystemTable->ConOut->EnableCursor(SystemTable->ConOut, TRUE);// 开启控制台光标显示

    // =========================================================================
    // 1. 开辟一块内存，用于存放 Bootloader 传递给 OS Kernel 的系统环境参数
    // =========================================================================
    BootInfo_t *BootInfo;
    // 使用 EfiLoaderData 分配。退出引导服务后，内核依然可以安全读取这块内存
    gBS->AllocatePool(EfiLoaderData, sizeof(BootInfo_t), (void*)&BootInfo);


    // =========================================================================
    // 2. 文本模式配置
    // =========================================================================
    UINTN Columns, Rows;
    for(UINT32 i = 0; i < SystemTable->ConOut->Mode->MaxMode; i++) {
        SystemTable->ConOut->QueryMode(SystemTable->ConOut, i, &Columns, &Rows);
        Print(L"TextMode:%2d    Columns:%4d    Rows:%4d\n", i, Columns, Rows);
    }

    SystemTable->ConOut->QueryMode(SystemTable->ConOut, SystemTable->ConOut->Mode->Mode, &Columns, &Rows);
    Print(L"CurrenTextMode:%2d    Columns:%4d    Rows:%4d\n", SystemTable->ConOut->Mode->Mode, Columns, Rows);
    Print(L"Please enter text mode or keep default: ");

    Status = keyCountdown(30);
    if(Status) {
        while(1) {
            CHAR16 InputBuffer[5];
            UINT32 InputBufferLength = sizeof(InputBuffer)/sizeof(CHAR16);
            UINT32 Mode = 0;

            PrintInput(InputBuffer, &InputBufferLength);

            // ASCII 字符转整型
            for(UINT32 i = 0; i < InputBufferLength; i++) {
                if(InputBuffer[i] < 0x30 || InputBuffer[i] > 0x39){
                    Mode = 0xFFFF;
                    break;
                }
                Mode = Mode * 10 + (InputBuffer[i] - 0x30);
            }

            Status = SystemTable->ConOut->SetMode(SystemTable->ConOut, Mode);
            if(!EFI_ERROR(Status))
                break;

            for(UINT32 i = 0; i < InputBufferLength; i++){
                    Print(L"\b");
            }
        }
    }
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);


    // =========================================================================
    // 3. 分辨率模式 (GOP: Graphics Output Protocol) 配置
    // =========================================================================
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gGraphicsOutput = 0;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info = 0;
    UINTN InfoSize = 0;

    SystemTable->ConOut->QueryMode(SystemTable->ConOut, SystemTable->ConOut->Mode->Mode, &Columns, &Rows);
    gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&gGraphicsOutput);

    for(UINT32 i = 0; i < gGraphicsOutput->Mode->MaxMode; i++) {
        gGraphicsOutput->QueryMode(gGraphicsOutput, i, &InfoSize, &Info);
        if((SystemTable->ConOut->Mode->CursorColumn + 45) > Columns)
            Print(L"\n");

        Print(L"ResolutionMode:%2d H:%4d V:%4d P:%4d F:%d   ",
              i, Info->HorizontalResolution, Info->VerticalResolution, Info->PixelsPerScanLine, Info->PixelFormat);
        gBS->FreePool(Info);
    }
    Print(L"\n");

    Print(L"CRMode:%2d H:%d V:%d FrameBase:0x%lx FrameSize:0x%lx\n",
          gGraphicsOutput->Mode->Mode,
          gGraphicsOutput->Mode->Info->HorizontalResolution,
          gGraphicsOutput->Mode->Info->VerticalResolution,
          gGraphicsOutput->Mode->FrameBufferBase,
          gGraphicsOutput->Mode->FrameBufferSize);

    Print(L"Please enter a resolution mode or keep the default: ");
    Status = keyCountdown(30);
    if(Status) {
        while(1) {
            CHAR16 InputBuffer[5];
            UINT32 InputBufferLength = sizeof(InputBuffer) / sizeof(CHAR16);
            UINT32 Mode = 0;

            PrintInput(InputBuffer, &InputBufferLength);
            for(UINT32 i = 0; i < InputBufferLength; i++) {
                if(InputBuffer[i] < 0x30 || InputBuffer[i] > 0x39) {
                    Mode = 0xFFFF;
                    break;
                }
                Mode = Mode * 10 + (InputBuffer[i] - 0x30);
            }

            Status = gGraphicsOutput->SetMode(gGraphicsOutput, Mode);
            if(!EFI_ERROR(Status))
                break;

            for(UINT32 i = 0; i < InputBufferLength; i++) {
                Print(L"\b");
            }
        }
    }

    BootInfo->FrameBufferBase = gGraphicsOutput->Mode->FrameBufferBase;
    BootInfo->FrameBufferSize = gGraphicsOutput->Mode->FrameBufferSize;
    BootInfo->HorizontalResolution = gGraphicsOutput->Mode->Info->HorizontalResolution;
    BootInfo->VerticalResolution = gGraphicsOutput->Mode->Info->VerticalResolution;
    BootInfo->PixelsPerScanLine = gGraphicsOutput->Mode->Info->PixelsPerScanLine;

    gBS->CloseProtocol(gGraphicsOutput, &gEfiGraphicsOutputProtocolGuid, ImageHandle, NULL);
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);


    // =========================================================================
    // 4. 获取 ACPI 表 (高级配置与电源接口)
    // =========================================================================
    RSDP_Struct *RSDP;
    EfiGetSystemConfigurationTable(&gEfiAcpiTableGuid, (void*)&RSDP);
    Print(L"RSDP:%lx   XSDT:%lx\n", RSDP, RSDP->XsdtAddress);

    BootInfo->RSDP = RSDP;


    // =========================================================================
    // 5. 定位并动态加载操作系统内核文件 (kernel.bin)
    // =========================================================================
    EFI_LOADED_IMAGE        *LoadedImage;
    EFI_DEVICE_PATH         *DevicePath;
    EFI_FILE_IO_INTERFACE   *Vol;
    EFI_FILE_HANDLE         RootFs;
    EFI_FILE_HANDLE         FileHandle;
    EFI_DEVICE_PATH_TO_TEXT_PROTOCOL* Device2TextProtocol = 0;

    gBS->LocateProtocol(&gEfiDevicePathToTextProtocolGuid, NULL, (VOID**)&Device2TextProtocol);
    gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID*)&LoadedImage);
    gBS->HandleProtocol(LoadedImage->DeviceHandle, &gEfiDevicePathProtocolGuid, (VOID*)&DevicePath);

    CHAR16* TextDevicePath = Device2TextProtocol->ConvertDevicePathToText(DevicePath, FALSE, TRUE);
    Print(L"%s\n", TextDevicePath);
    if(TextDevicePath)
        gBS->FreePool(TextDevicePath);

    WalkthroughDevicePath(DevicePath, PrintNode);
    Print(L"\n");

    gBS->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID*)&Vol);
    Vol->OpenVolume(Vol, &RootFs);
    RootFs->Open(RootFs, &FileHandle, (CHAR16*)L"kernel.bin", EFI_FILE_MODE_READ, 0);

    EFI_FILE_INFO* FileInfo;
    UINTN BufferSize = 0;

    // 🌟 核心修复 1：声明地址并赋初值为 4GB 边界 (0xFFFFFFFF)
    // 这是给 AllocateMaxAddress 划定的分配上限，确保内核绝对加载在低 4GB 内存中
    EFI_PHYSICAL_ADDRESS kernel_entry = 0xFFFFFFFF;

    BufferSize = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 100;
    Status = gBS->AllocatePool(EfiLoaderData, BufferSize, (VOID**)&FileInfo);
    if (EFI_ERROR(Status)) {
        Print(L"Error: AllocatePool for FileInfo failed!\n");
        while(1) CpuBreakpoint();
    }

    // 🌟 核心修复 2：必须检查 GetInfo 是否成功，防止 FileSize 产生天文数字乱码
    Status = FileHandle->GetInfo(FileHandle, &gEfiFileInfoGuid, &BufferSize, FileInfo);
    if (EFI_ERROR(Status)) {
        Print(L"Error: GetInfo failed, Status: %r\n", Status);
        while(1) CpuBreakpoint();
    }

    if (FileInfo->FileSize == 0) {
        Print(L"Error: Kernel file is empty!\n");
        while(1) CpuBreakpoint();
    }

    Print(L"\tFileName:%s\t Size:%d\t FileSize:%d\t Physical Size:%d\n",
          FileInfo->FileName, FileInfo->Size, FileInfo->FileSize, FileInfo->PhysicalSize);

    // 精确计算需要的物理页数 (4KB 对齐)
    UINTN pages_needed = (FileInfo->FileSize + 0xFFF) >> 12;
    Print(L"Requesting %d pages below 4GB...\n", pages_needed);

    // 🌟 核心修复 3：改用 AllocateMaxAddress 策略
    // UEFI 会在 kernel_entry (0xFFFFFFFF) 以下寻找空闲内存，完美避开高位 DMA 卡死陷阱
    Status = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, pages_needed, &kernel_entry);

    if (EFI_ERROR(Status)) {
        Print(L"Error: AllocatePages failed, Status: %r\n", Status);
        while(1) CpuBreakpoint(); // 如果内存不够或碎片化严重，死机并暴露错误
    }

    Print(L"Kernel Allocated at: 0x%lx\n", kernel_entry);

    // 正常读取文件到分配好的地址
    BufferSize = FileInfo->FileSize;
    Status = FileHandle->Read(FileHandle, &BufferSize, (VOID*)kernel_entry);
    if (EFI_ERROR(Status)) {
        Print(L"Error: Read kernel file failed, Status: %r\n", Status);
        while(1) CpuBreakpoint();
    }

    // 记录进 BootInfo 传给内核
    BootInfo->KernelBase = kernel_entry;
    BootInfo->KernelSize = FileInfo->FileSize;

    gBS->FreePool(FileInfo);
    FileHandle->Close(FileHandle);
    RootFs->Close(RootFs);

    gBS->CloseProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, ImageHandle, NULL);
    gBS->CloseProtocol(LoadedImage->DeviceHandle, &gEfiDevicePathProtocolGuid, ImageHandle, NULL);
    gBS->CloseProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, ImageHandle, NULL);
    gBS->CloseProtocol(Device2TextProtocol, &gEfiDevicePathToTextProtocolGuid, ImageHandle, NULL);


    // =========================================================================
    // 6. 获取系统内存地图 (Memory Map) 并退出引导服务
    // =========================================================================
    UINTN MemMapSize = 0;
    EFI_MEMORY_DESCRIPTOR* MemMap = 0;
    UINTN MapKey = 0;
    UINTN DescriptorSize = 0;
    UINT32 DesVersion = 0;

    Print(L"Get Memory Map\n");
    gBS->GetMemoryMap(&MemMapSize, MemMap, &MapKey, &DescriptorSize, &DesVersion);
    gBS->AllocatePool(EfiLoaderData, MemMapSize, (VOID**)&MemMap);
    gBS->GetMemoryMap(&MemMapSize, MemMap, &MapKey, &DescriptorSize, &DesVersion);

    // 第三次确保获取绝对最新的 MapKey
    gBS->GetMemoryMap(&MemMapSize, MemMap, &MapKey, &DescriptorSize, &DesVersion);

    BootInfo->MemMap = MemMap;
    BootInfo->MemMapSize = MemMapSize;
    BootInfo->MemDescriptorSize = DescriptorSize;
    BootInfo->DesVersion = DesVersion;
    BootInfo->gRTS = SystemTable->RuntimeServices;

    // 退出 UEFI 启动时服务，将内存与硬件管控大权交还给将要运行的内核
    Status = gBS->ExitBootServices(ImageHandle, MapKey);
    if(EFI_ERROR(Status))
        Print(L"ERROR: %r. Failed to Boot/gBS->ExitBootService().\n", Status);


    // =========================================================================
    // 7. 进入内核执行 (Jump to Kernel)
    // =========================================================================

    // 🌟 终极升级：将入口点强转为刚分配的动态随机地址 pages。
    // RDI 寄存器将携带 BootInfo 的指针作为参数传给你的 Kernel 入口汇编代码。
    void (*KernelEntryPoint)(BootInfo_t *BootInfo) = (void *)kernel_entry;

    KernelEntryPoint(BootInfo);

    return EFI_SUCCESS;
}