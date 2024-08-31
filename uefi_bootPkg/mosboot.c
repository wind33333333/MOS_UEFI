#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle,EFI_SYSTEM_TABLE *SystemTable){

   CpuBreakpoint();
    for(int i = 0; i < 10; i++){
        Print(L"Hello World MOSBOOT!\n");
        Print(L"Hello World MOSBOOT!\n");
        Print(L"Hello World MOSBOOT!\n");
    }
    
//    while(11);

    return EFI_SUCCESS;
}
