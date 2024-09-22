#ifndef __MLIB__
#define __MLIB__

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS EFIAPI keyCountdown (IN EFI_SYSTEM_TABLE* SystemTable,IN UINT32 Times);
EFI_STATUS EFIAPI PrintInput (IN EFI_SYSTEM_TABLE* systemTable,IN OUT CHAR16* InputBuffer,IN OUT UINT32* InputBufferLength);


#endif