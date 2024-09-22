#include "mlib.h"

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,IN EFI_SYSTEM_TABLE* SystemTable){

    CpuBreakpoint();
    EFI_STATUS Status;

    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);   //清空屏幕
    SystemTable->ConOut->EnableCursor(SystemTable->ConOut,TRUE); //显示光标

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
    Status=keyCountdown(SystemTable,30);
    if(Status){
        while(1){
            CHAR16 InputBuffer[5];
            UINT32 InputBufferLength = sizeof(InputBuffer)/sizeof(CHAR16);
            UINT32 Mode = 0;
            PrintInput(SystemTable, InputBuffer,&InputBufferLength);
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
    gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid,NULL,(VOID **)&gGraphicsOutput);
    for(UINT32 i = 0;i < gGraphicsOutput->Mode->MaxMode;i++){
        gGraphicsOutput->QueryMode(gGraphicsOutput,i,&InfoSize,&Info);
        if((SystemTable->ConOut->Mode->CursorColumn+31)>Columns)
            Print(L"\n");
        Print(L"ResolutionMode:%2d H:%4d V:%4d    ",i,Info->HorizontalResolution,Info->VerticalResolution);
        gBS->FreePool(Info);
    }
    Print(L"\n");
    Print(L"CRMode:%2d H:%d V:%d FrameBase:0x%lx FrameSize:0x%lx\n",gGraphicsOutput->Mode->Mode,gGraphicsOutput->Mode->Info->HorizontalResolution,gGraphicsOutput->Mode->Info->VerticalResolution,gGraphicsOutput->Mode->FrameBufferBase,gGraphicsOutput->Mode->FrameBufferSize);
    //输入分辨率模式
    Print(L"Please enter a resolution mode or keep the default: ");
    Status=keyCountdown(SystemTable,30);
    if(Status){
        while(1){
            CHAR16 InputBuffer[5];
            UINT32 InputBufferLength = sizeof(InputBuffer) / sizeof(CHAR16);
            UINT32 Mode = 0;
            PrintInput(SystemTable, InputBuffer,&InputBufferLength);
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
    gBS->CloseProtocol(gGraphicsOutput,&gEfiGraphicsOutputProtocolGuid,ImageHandle,NULL);
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);   //清空屏幕
    //endregion

    //region 内存获取
    UINTN MemMapSize = 0;
    EFI_MEMORY_DESCRIPTOR* MemMap = 0;
    UINTN MapKey = 0;
    UINTN DescriptorSize = 0;
    UINT32 DesVersion = 0;

    Print(L"Get EFI_MEMORY_DESCRIPTOR Structure\n");
    gBS->GetMemoryMap(&MemMapSize,MemMap,&MapKey,&DescriptorSize,&DesVersion);
    gBS->AllocatePool(EfiRuntimeServicesData,MemMapSize,(VOID**)&MemMap);
    gBS->GetMemoryMap(&MemMapSize,MemMap,&MapKey,&DescriptorSize,&DesVersion);

    for(UINT32 i = 0; i< MemMapSize / DescriptorSize; i++){
        EFI_MEMORY_DESCRIPTOR* MMap = (EFI_MEMORY_DESCRIPTOR*) (((CHAR8*)MemMap) + i * DescriptorSize);
        Print(L"MMap %4d %10d (%10lx~%10lx) %016lx\n",MMap->Type,MMap->NumberOfPages,MMap->PhysicalStart,MMap->PhysicalStart + (MMap->NumberOfPages << 12),MMap->Attribute);
    }
    gBS->FreePool(MemMap);
    //endregion

    while(1);

    return EFI_SUCCESS;
}

