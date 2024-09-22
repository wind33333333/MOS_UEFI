#include "mlib.h"



EFI_STATUS EFIAPI keyCountdown (IN EFI_SYSTEM_TABLE* SystemTable,UINT32 Times){
    EFI_INPUT_KEY Key;
    while(Times){
        Print(L"%3ds",Times);
        SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key); // 读取按键
        gBS->Stall(1000000);
        Print(L"\b\b\b\b");
        Times--;
        if(Key.ScanCode || Key.UnicodeChar)
            return 1;
    }
    return 0;
}


EFI_STATUS EFIAPI PrintInput (IN EFI_SYSTEM_TABLE* SystemTable,IN OUT CHAR16* InputBuffer,IN OUT UINT32* InputBufferLength){

    EFI_INPUT_KEY Key;
    EFI_EVENT WaitList[1];  // 事件列表，可以包含多个事件
    UINT32 InputIndex=0;

    (*InputBufferLength)--;
    WaitList[0] = SystemTable->ConIn->WaitForKey;
    while(1){
        gBS->WaitForEvent(1, WaitList, NULL);
        SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key); // 读取按键

        if(Key.UnicodeChar>=0x20 && Key.UnicodeChar<=0x7E && InputIndex < *InputBufferLength){
            Print(L"%c", Key.UnicodeChar);
            InputBuffer[InputIndex]=Key.UnicodeChar;
            InputIndex++;
        }else if(Key.UnicodeChar==0x8 && InputIndex>0){//退格
            Print(L"%c",Key.UnicodeChar);
            InputIndex--;
        }else if(Key.UnicodeChar==0xD && InputIndex>0){//回车
            InputBuffer[InputIndex]=0;
            *InputBufferLength = InputIndex;
            break;
        }
    }
    return 0;
}