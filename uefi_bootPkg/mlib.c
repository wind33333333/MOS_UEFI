#include "mlib.h"

EFI_STATUS EFIAPI PrintInput (IN EFI_HANDLE ImageHandle,IN EFI_SYSTEM_TABLE* SystemTable){

    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gGraphicsOutput = 0;
    EFI_INPUT_KEY Key;
    EFI_EVENT WaitList[1];  // 事件列表，可以包含多个事件
    CHAR16 inputbuffer[5];
    UINT32 inputindex=0;
    UINT32 resolutionmode =0;         //分辨率模式号
    UINT32 time=30;

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

    return Status;
}