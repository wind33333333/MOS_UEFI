#include "mlib.h"

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,IN EFI_SYSTEM_TABLE* SystemTable){

    CpuBreakpoint();
    EFI_STATUS Status;

    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);   //清空屏幕
    SystemTable->ConOut->EnableCursor(SystemTable->ConOut,TRUE); //显示光标

    //region 开辟一块内存存放boot传递给kernel的参数
    BootInfo_struct *BootInfo;
    gBS->AllocatePool(EfiRuntimeServicesData,sizeof(BootInfo),(void*)&BootInfo);
    //endregion

    //region 文本模式
    UINTN Columns, Rows;
    for(UINT32 i=0;i<SystemTable->ConOut->Mode->MaxMode;i++){
        SystemTable->ConOut->QueryMode(SystemTable->ConOut,i,&Columns,&Rows);
        Print(L"TextMode:%2d    Columns:%4d    Rows:%4d\n",i,Columns,Rows);
    }
    //打印当前文本模式
    SystemTable->ConOut->QueryMode(SystemTable->ConOut,SystemTable->ConOut->Mode->Mode,&Columns,&Rows);
    Print(L"CurrenTextMode:%2d    Columns:%4d    Rows:%4d\n",SystemTable->ConOut->Mode->Mode,Columns,Rows);
    Print(L"Please enter text mode or keep default: ");

    //设置文本模式
    Status=keyCountdown(30);
    if(Status){
        while(1){
            CHAR16 InputBuffer[5];
            UINT32 InputBufferLength = sizeof(InputBuffer)/sizeof(CHAR16);
            UINT32 Mode = 0;
            PrintInput(InputBuffer,&InputBufferLength);
            for(UINT32 i=0;i<InputBufferLength;i++){
                if(InputBuffer[i]<0x30 || InputBuffer[i]>0x39){
                    Mode = 0xFFFF;
                    break;
                }
                Mode = Mode * 10 + (InputBuffer[i] - 0x30);
            }

            Status = SystemTable->ConOut->SetMode(SystemTable->ConOut, Mode);
            if(!EFI_ERROR(Status))
                break;

            for(UINT32 i = 0;i<InputBufferLength;i++){
                    Print(L"\b");
            }
        }
    }
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);   //清空屏幕
    //endregion

    //region 分辨率模式
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gGraphicsOutput = 0;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info = 0;
    UINTN InfoSize = 0;
    SystemTable->ConOut->QueryMode(SystemTable->ConOut,SystemTable->ConOut->Mode->Mode,&Columns,&Rows);
    gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid,NULL,(VOID **)&gGraphicsOutput);
    for(UINT32 i = 0;i < gGraphicsOutput->Mode->MaxMode;i++){
        gGraphicsOutput->QueryMode(gGraphicsOutput,i,&InfoSize,&Info);
        if((SystemTable->ConOut->Mode->CursorColumn+34)>Columns)
            Print(L"\n");
        Print(L"ResolutionMode:%2d H:%4d V:%4d   ",i,Info->HorizontalResolution,Info->VerticalResolution);
        gBS->FreePool(Info);
    }
    Print(L"\n");
    Print(L"CRMode:%2d H:%d V:%d FrameBase:0x%lx FrameSize:0x%lx\n",gGraphicsOutput->Mode->Mode,gGraphicsOutput->Mode->Info->HorizontalResolution,gGraphicsOutput->Mode->Info->VerticalResolution,gGraphicsOutput->Mode->FrameBufferBase,gGraphicsOutput->Mode->FrameBufferSize);
    //输入分辨率模式
    Print(L"Please enter a resolution mode or keep the default: ");
    Status=keyCountdown(30);
    if(Status){
        while(1){
            CHAR16 InputBuffer[5];
            UINT32 InputBufferLength = sizeof(InputBuffer) / sizeof(CHAR16);
            UINT32 Mode = 0;
            PrintInput(InputBuffer,&InputBufferLength);
            for(UINT32 i = 0;i<InputBufferLength;i++){
                if(InputBuffer[i]<0x30 || InputBuffer[i]>0x39){
                    Mode = 0xFFFF;
                    break;
                }
                Mode = Mode * 10 + (InputBuffer[i] - 0x30);
            }
            Status=gGraphicsOutput->SetMode(gGraphicsOutput,Mode);
            if(!EFI_ERROR(Status))
                break;

            for(UINT32 i = 0;i<InputBufferLength;i++){
                Print(L"\b");
            }
        }
    }
    BootInfo->FrameBufferBase=gGraphicsOutput->Mode->FrameBufferBase;
    BootInfo->FrameBufferSize=gGraphicsOutput->Mode->FrameBufferSize;
    BootInfo->HorizontalResolution=gGraphicsOutput->Mode->Info->HorizontalResolution;
    BootInfo->VerticalResolution=gGraphicsOutput->Mode->Info->VerticalResolution;
    gBS->CloseProtocol(gGraphicsOutput,&gEfiGraphicsOutputProtocolGuid,ImageHandle,NULL);
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);   //清空屏幕
    //endregion

    //region 获取ACPI
    RSDP_Struct *RSDP;
    EfiGetSystemConfigurationTable(&gEfiAcpiTableGuid, (void*)&RSDP);
    Print(L"RSDP:%0xlx XSDT:%0xlx\n",RSDP,RSDP->XsdtAddress);
    BootInfo->RSDP=RSDP;
    //endregion

    //region 读取kernel.bin

    EFI_LOADED_IMAGE        *LoadedImage;
    EFI_DEVICE_PATH         *DevicePath;
    EFI_FILE_IO_INTERFACE   *Vol;
    EFI_FILE_HANDLE         RootFs;
    EFI_FILE_HANDLE         FileHandle;

    EFI_DEVICE_PATH_TO_TEXT_PROTOCOL* Device2TextProtocol = 0;

    gBS->LocateProtocol(&gEfiDevicePathToTextProtocolGuid,NULL,(VOID**)&Device2TextProtocol);
    gBS->HandleProtocol(ImageHandle,&gEfiLoadedImageProtocolGuid,(VOID*)&LoadedImage);
    gBS->HandleProtocol(LoadedImage->DeviceHandle,&gEfiDevicePathProtocolGuid,(VOID*)&DevicePath);

    CHAR16* TextDevicePath = Device2TextProtocol->ConvertDevicePathToText(DevicePath,FALSE,TRUE);
    Print(L"%s\n",TextDevicePath);
    if(TextDevicePath)
        gBS->FreePool(TextDevicePath);
    WalkthroughDevicePath(DevicePath,PrintNode);
    Print(L"\n");

    gBS->HandleProtocol(LoadedImage->DeviceHandle,&gEfiSimpleFileSystemProtocolGuid,(VOID*)&Vol);
    Vol->OpenVolume(Vol,&RootFs);
    RootFs->Open(RootFs,&FileHandle,(CHAR16*)L"kernel.bin",EFI_FILE_MODE_READ,0);

    EFI_FILE_INFO* FileInfo;
    UINTN BufferSize = 0;
    EFI_PHYSICAL_ADDRESS pages = KERNELSTARTADDR;

    BufferSize = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 100;
    gBS->AllocatePool(EfiRuntimeServicesData,BufferSize,(VOID**)&FileInfo);
    FileHandle->GetInfo(FileHandle,&gEfiFileInfoGuid,&BufferSize,FileInfo);
    Print(L"\tFileName:%s\t Size:%d\t FileSize:%d\t Physical Size:%d\n",FileInfo->FileName,FileInfo->Size,FileInfo->FileSize,FileInfo->PhysicalSize);

    Print(L"Read kernel file to memory\n");
    gBS->AllocatePages(AllocateAddress,EfiRuntimeServicesData,(FileInfo->FileSize + 0x1000 - 1) / 0x1000,&pages);
    BufferSize = FileInfo->FileSize;
    FileHandle->Read(FileHandle,&BufferSize,(VOID*)pages);
    gBS->FreePool(FileInfo);
    FileHandle->Close(FileHandle);
    RootFs->Close(RootFs);

    gBS->CloseProtocol(LoadedImage->DeviceHandle,&gEfiSimpleFileSystemProtocolGuid,ImageHandle,NULL);
    gBS->CloseProtocol(LoadedImage->DeviceHandle,&gEfiDevicePathProtocolGuid,ImageHandle,NULL);
    gBS->CloseProtocol(ImageHandle,&gEfiLoadedImageProtocolGuid,ImageHandle,NULL);
    gBS->CloseProtocol(Device2TextProtocol,&gEfiDevicePathToTextProtocolGuid,ImageHandle,NULL);

    //endregion

    //region 内存图获取释放boot进入kernel 位于0x100000
    UINTN MemMapSize = 0;
    EFI_MEMORY_DESCRIPTOR* MemMap = 0;
    UINTN MapKey = 0;
    UINTN DescriptorSize = 0;
    UINT32 DesVersion = 0;

    Print(L"Get Memory Map\n");
    gBS->GetMemoryMap(&MemMapSize,MemMap,&MapKey,&DescriptorSize,&DesVersion);
    gBS->AllocatePool(EfiRuntimeServicesData,MemMapSize,(VOID**)&MemMap);
    gBS->GetMemoryMap(&MemMapSize,MemMap,&MapKey,&DescriptorSize,&DesVersion);

    for(UINT32 i = 0; i< MemMapSize / DescriptorSize; i++){
        EFI_MEMORY_DESCRIPTOR* MMap = (EFI_MEMORY_DESCRIPTOR*) (((CHAR8*)MemMap) + i * DescriptorSize);
        Print(L"M:%3d T:%2d A:%16lx N:%16lx S:%16lx E:%16lx\n",i,MMap->Type,MMap->Attribute,MMap->NumberOfPages,MMap->PhysicalStart,MMap->PhysicalStart + (MMap->NumberOfPages << 12)-1);
    }

    gBS->GetMemoryMap(&MemMapSize,MemMap,&MapKey,&DescriptorSize,&DesVersion);
    BootInfo->MemMap=MemMap;
    BootInfo->MemMapSize=MemMapSize;
    BootInfo->MemDescriptorSize=DescriptorSize;
    BootInfo->gRTS=SystemTable->RuntimeServices;
    Status=gBS->ExitBootServices(ImageHandle,MapKey);
    if(EFI_ERROR(Status))
        Print(L"ERROR: %r. Failed to Boot/gBS->ExitBootService().\n",Status);

    //进入内核
    void (*KernelEntryPoint)(BootInfo_struct* BootInfo) = (void(*)(BootInfo_struct* BootInfo))KERNELSTARTADDR;
    KernelEntryPoint(BootInfo);
    //endregion

    return EFI_SUCCESS;
}

