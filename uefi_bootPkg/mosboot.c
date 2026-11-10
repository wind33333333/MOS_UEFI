#include "mosbootlib.h"

// =========================================================================
// 安全策略 1：将核心环境参数静态分配，彻底规避第一个 AllocatePool
// =========================================================================
static BootInfo_t g_BootInfo = {0};

// UEFI 应用程序的入口函数
EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable) {

    // CpuBreakpoint();  // 启用调试模式，触发断点异常，供连接的 GDB 或调试器捕获

    EFI_STATUS Status;
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);       // 清空控制台屏幕
    SystemTable->ConOut->EnableCursor(SystemTable->ConOut, TRUE);// 开启控制台光标显示

    BootInfo_t *BootInfo = &g_BootInfo; // 指向静态分配的全局区，绝对安全

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
    // 5. 定位并动态加载操作系统内核文件 (实现 2MB 对齐与碎片回收)
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

    // =========================================================================
    // 安全策略 2：彻底消灭分配 FileInfo 时导致的卡死，直接使用栈数组分配！
    // =========================================================================
    UINT8 FileInfoBuffer[512]; // 栈上直接分配 512 字节，安全极速
    EFI_FILE_INFO* FileInfo = (EFI_FILE_INFO*)FileInfoBuffer;
    UINTN BufferSize = sizeof(FileInfoBuffer);

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

    // -------------------------------------------------------------------------
    // 🌟 KASLR 2MB 对齐核心算法：申请冗余、强行对齐、掐头去尾
    // -------------------------------------------------------------------------

    // 1. 申请冗余空间：按最大漂移容忍度，多申请 2MB (0x1FFFFF) 的物理页
    UINTN pages_needed = (FileInfo->FileSize + 0x1FFFFF) >> 12;
    EFI_PHYSICAL_ADDRESS raw_alloc = 0xFFFFFFFF; // 限制在 4GB 以下

    Status = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, pages_needed, &raw_alloc);
    if (EFI_ERROR(Status)) {
        Print(L"Error: AllocatePages failed, Status: %r\n", Status);
        while(1) CpuBreakpoint();
    }

    // 2. 计算 2MB 向上对齐的真实内核基址
    EFI_PHYSICAL_ADDRESS kernel_entry = (raw_alloc + 0x1FFFFF) & ~0x1FFFFFULL;

    // 3. 掐头 (释放头部的碎片内存)
    UINTN front_waste_pages = 0;
    if (kernel_entry > raw_alloc) {
        front_waste_pages = (kernel_entry - raw_alloc) >> 12;
        gBS->FreePages(raw_alloc, front_waste_pages);
        Print(L"\tFreed %d pages at front (Head Truncation).\n", front_waste_pages);
    }

    // 4. 去尾 (释放尾部的冗余内存)
    UINTN actual_kernel_pages = (FileInfo->FileSize + 0xFFF) >> 12; // 真实需要的 4KB 对齐页数
    UINTN back_waste_pages = pages_needed - front_waste_pages - actual_kernel_pages;

    if (back_waste_pages > 0) {
        EFI_PHYSICAL_ADDRESS back_waste_start = kernel_entry + (actual_kernel_pages << 12);
        gBS->FreePages(back_waste_start, back_waste_pages);
        Print(L"\tFreed %d pages at back (Tail Truncation).\n", back_waste_pages);
    }

    Print(L"\tKernel strictly 2MB-aligned at: 0x%lx (Using %d pages)\n", kernel_entry, actual_kernel_pages);

    // -------------------------------------------------------------------------

    // 正常读取文件到对齐后的纯净基址中
    BufferSize = FileInfo->FileSize;
    Status = FileHandle->Read(FileHandle, &BufferSize, (VOID*)kernel_entry);
    if (EFI_ERROR(Status)) {
        Print(L"Error: Read kernel file failed, Status: %r\n", Status);
        while(1) CpuBreakpoint();
    }

    // 记录进 BootInfo 传给内核 (此时的 KernelBase 低 21 位必全为 0)
    BootInfo->KernelBase = kernel_entry;
    BootInfo->KernelSize = FileInfo->FileSize;

    // gBS->FreePool(FileInfo);  // 注意：因为改为栈分配，此处必须移除！
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
    // =========================================================================
    // 安全策略 3：如果你希望连 MemMap 的 Pool 也彻底替换，可以使用 AllocatePages。
    // 但鉴于你之前这里没报错，为了代码改动最小，暂保留 AllocatePool。
    // =========================================================================
    gBS->GetMemoryMap(&MemMapSize, MemMap, &MapKey, &DescriptorSize, &DesVersion);

    // 给 MemMapSize 增加足够的冗余量，防止两次调用期间有新的分配产生导致 Buffer Too Small
    MemMapSize += DescriptorSize * 16;

    Status = gBS->AllocatePool(EfiLoaderData, MemMapSize, (VOID**)&MemMap);
    if (EFI_ERROR(Status)) {
        Print(L"Error: AllocatePool for MemMap failed, Status: %r\n", Status);
        while(1) CpuBreakpoint();
    }

    Status = gBS->GetMemoryMap(&MemMapSize, MemMap, &MapKey, &DescriptorSize, &DesVersion);
    if (EFI_ERROR(Status)) {
        Print(L"Error: GetMemoryMap failed, Status: %r\n", Status);
        while(1) CpuBreakpoint();
    }

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

    // 🌟 终极升级：将入口点强转为刚分配并严格对齐的动态随机地址 kernel_entry。
    // RDI 寄存器将携带 BootInfo 的指针作为参数传给你的 Kernel 入口汇编代码。
    void (*KernelEntryPoint)(BootInfo_t *BootInfo) = (void *)kernel_entry;

    KernelEntryPoint(BootInfo);

    return EFI_SUCCESS;
}