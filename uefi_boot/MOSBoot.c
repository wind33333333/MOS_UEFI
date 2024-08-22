#include <Uefi.h>
#include <Library/UefiLib.h>

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle,EFI_SYSTEM_TABLE *SystemTable){

    for(int i = 0; i < 10; i++){
        Print(L"Hello World MOSBOOT!\n");
    }
    while(11);

    return EFI_SUCCESS;
}
