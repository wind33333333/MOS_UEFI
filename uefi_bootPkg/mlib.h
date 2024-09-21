#ifndef __MLIB__
#define __MLIB__

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS EFIAPI PrintInput (IN EFI_HANDLE ImageHandle,IN EFI_SYSTEM_TABLE *systemTable);

#endif