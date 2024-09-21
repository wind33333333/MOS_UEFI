#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,IN EFI_SYSTEM_TABLE *SystemTable){

    CpuBreakpoint();
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gGraphicsOutput = 0;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info = 0;
    UINTN InfoSize = 0;
    UINTN Columns, Rows;
    EFI_INPUT_KEY Key;
    EFI_EVENT WaitList[1];  // 事件列表，可以包含多个事件
    CHAR16 inputbuffer[5];
    UINT32 inputindex=0;
    UINT32 resolutionmode =0;         //分辨率模式号
    UINT32 time=30;

    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);   //清空屏幕
    SystemTable->ConOut->EnableCursor(SystemTable->ConOut,TRUE); //显示光标



    //分辨率配置
    for(UINT32 i=0;i<SystemTable->ConOut->Mode->MaxMode;i++){  //打印所有文本模式
        SystemTable->ConOut->QueryMode(SystemTable->ConOut,i,&Columns,&Rows);
        Print(L"TextMode:%d Columns:%d Rows:%d\n",i,Columns,Rows);
    }
    //打印当前文本模式
    SystemTable->ConOut->QueryMode(SystemTable->ConOut,SystemTable->ConOut->Mode->Mode,&Columns,&Rows);
    Print(L"CurrenTextMode:%d Columns:%d Rows:%d\n",SystemTable->ConOut->Mode->Mode,Columns,Rows);

    gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid,NULL,(VOID **)&gGraphicsOutput);
    for(UINT32 i = 0;i < gGraphicsOutput->Mode->MaxMode;i++){ //打印所有分辨率模式
        gGraphicsOutput->QueryMode(gGraphicsOutput,i,&InfoSize,&Info);
        if((SystemTable->ConOut->Mode->CursorColumn+20)>Columns)
            Print(L"\n");
        Print(L"Mode:%d %d*%d   ",i,Info->HorizontalResolution,Info->VerticalResolution);
        gBS->FreePool(Info);
    }
    Print(L"\n");
    Print(L"CurrenMode:%d %d*%d FrameBufferBase:0x%lx FrameBufferSize:0x%lx\n",gGraphicsOutput->Mode->Mode,gGraphicsOutput->Mode->Info->HorizontalResolution,gGraphicsOutput->Mode->Info->VerticalResolution,gGraphicsOutput->Mode->FrameBufferBase,gGraphicsOutput->Mode->FrameBufferSize);
    //输入分辨率模式
    Print(L"Please enter a resolution mode or keep the default:");
    SystemTable->ConIn->Reset(SystemTable->ConIn,FALSE);
    while(time){
        Print(L"%02ds",time);
        SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key); // 读取按键
        gBS->Stall(1000000);
        Print(L"\b\b\b");
        time--;
        if(Key.ScanCode || Key.UnicodeChar){
            time=TRUE;
            break;
        }
    }
    WaitList[0] = SystemTable->ConIn->WaitForKey;
    while(time){
        gBS->WaitForEvent(1, WaitList, NULL);
        SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key); // 读取按键

        if(Key.UnicodeChar>=0x30 && Key.UnicodeChar<=0x39 && inputindex<5){
            Print(L"%c", Key.UnicodeChar);
            inputbuffer[inputindex]=Key.UnicodeChar;
            inputindex++;
        }else if(Key.UnicodeChar==0x8 && inputindex>0){//退格
            Print(L"%c",Key.UnicodeChar);
            inputindex--;
            inputbuffer[inputindex]=0;
        }else if(Key.UnicodeChar==0xD && inputindex>0){//回车
            resolutionmode=0;
            for(unsigned int i=0;i<inputindex;i++){
                resolutionmode=resolutionmode*10+(inputbuffer[i]-0x30);
            }

            Status=gGraphicsOutput->SetMode(gGraphicsOutput,resolutionmode);
            if(EFI_ERROR(Status)){
                for(unsigned int i=0;i<inputindex;i++){
                    Print(L"\b");
                }
                inputindex=0;
                continue;
            }
            gBS->CloseProtocol(gGraphicsOutput,&gEfiGraphicsOutputProtocolGuid,ImageHandle,NULL);
            break;
        }
    }

    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
    Print(L"Current Mode:%02d,Version:%x,Format:%d,Horizontal:%d,Vertical:%d,ScanLine:%d,FrameBufferBase:%010lx,FrameBufferSize:%010lx\n",gGraphicsOutput->Mode->Mode,gGraphicsOutput->Mode->Info->Version,gGraphicsOutput->Mode->Info->PixelFormat,gGraphicsOutput->Mode->Info->HorizontalResolution,gGraphicsOutput->Mode->Info->VerticalResolution,gGraphicsOutput->Mode->Info->PixelsPerScanLine,gGraphicsOutput->Mode->FrameBufferBase,gGraphicsOutput->Mode->FrameBufferSize);

    while(1);

    return EFI_SUCCESS;
}
