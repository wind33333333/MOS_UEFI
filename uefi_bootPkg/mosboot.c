#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,IN EFI_SYSTEM_TABLE *SystemTable){

    CpuBreakpoint();
    //EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gGraphicsOutput = 0;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info = 0;
    UINTN InfoSize = 0;
    unsigned int videomode = 0;
    UINTN Columns, Rows;
    EFI_INPUT_KEY Key;
    EFI_EVENT WaitList[1];  // 事件列表，可以包含多个事件

    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
    SystemTable->ConOut->EnableCursor(SystemTable->ConOut,TRUE);

    for(unsigned int i=0;i<SystemTable->ConOut->Mode->MaxMode;i++){
        SystemTable->ConOut->QueryMode(SystemTable->ConOut,i,&Columns,&Rows);
        Print(L"Mode:%d,Columns:%d,Rows:%d\n",i,Columns,Rows);
    }

    SystemTable->ConOut->QueryMode(SystemTable->ConOut,SystemTable->ConOut->Mode->Mode,&Columns,&Rows);

    gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid,NULL,(VOID **)&gGraphicsOutput);
    Print(L"Current Mode:%d %d*%d FrameBufferBase:0x%lx FrameBufferSize:0x%lx\n",gGraphicsOutput->Mode->Mode,gGraphicsOutput->Mode->Info->HorizontalResolution,gGraphicsOutput->Mode->Info->VerticalResolution,gGraphicsOutput->Mode->FrameBufferBase,gGraphicsOutput->Mode->FrameBufferSize);

    for(unsigned int i = 0;i < gGraphicsOutput->Mode->MaxMode;i++){
        gGraphicsOutput->QueryMode(gGraphicsOutput,i,&InfoSize,&Info);
        if((SystemTable->ConOut->Mode->CursorColumn+20)>Columns)
            Print(L"\n");
        Print(L"Mode:%d %d*%d   ",i,Info->HorizontalResolution,Info->VerticalResolution);
        gBS->FreePool(Info);
    }
    Print(L"\n");

    gGraphicsOutput->SetMode(gGraphicsOutput,videomode);
    gBS->CloseProtocol(gGraphicsOutput,&gEfiGraphicsOutputProtocolGuid,ImageHandle,NULL);
    Print(L"Current Mode:%02d,Version:%x,Format:%d,Horizontal:%d,Vertical:%d,ScanLine:%d,FrameBufferBase:%010lx,FrameBufferSize:%010lx\n",gGraphicsOutput->Mode->Mode,gGraphicsOutput->Mode->Info->Version,gGraphicsOutput->Mode->Info->PixelFormat,gGraphicsOutput->Mode->Info->HorizontalResolution,gGraphicsOutput->Mode->Info->VerticalResolution,gGraphicsOutput->Mode->Info->PixelsPerScanLine,gGraphicsOutput->Mode->FrameBufferBase,gGraphicsOutput->Mode->FrameBufferSize);

    Print(L"Enter resolution mode number:");
    SystemTable->ConIn->Reset(SystemTable->ConIn,FALSE);
    WaitList[0] = SystemTable->ConIn->WaitForKey;
    while(1){
        gBS->WaitForEvent(1, WaitList, NULL);
        SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key); // 读取按键
        if(Key.UnicodeChar>=0x30 && Key.UnicodeChar<=0x39)
        Print(L"%c", Key.UnicodeChar);
    }


    while(1);

    return EFI_SUCCESS;
}
