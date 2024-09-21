#ifndef __MLIB__
#define __MLIB__

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS EFIAPI keyCountdown (IN EFI_SYSTEM_TABLE* SystemTable,UINT32 Times);
EFI_STATUS EFIAPI PrintInput (IN EFI_SYSTEM_TABLE *systemTable,CHAR16* InputBuffer,UINT32 InputBufferLength);

#endif