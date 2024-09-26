#ifndef __MLIB__
#define __MLIB__

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DevicePathToText.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Guid/FileInfo.h>
#include <Library/DevicePathLib.h>

EFI_STATUS EFIAPI keyCountdown (IN UINT32 Times);
EFI_STATUS EFIAPI PrintInput (IN OUT CHAR16* InputBuffer,IN OUT UINT32* InputBufferLength);
EFI_DEVICE_PATH_PROTOCOL* WalkthroughDevicePath(EFI_DEVICE_PATH_PROTOCOL* DevPath, EFI_STATUS (*Callbk)(EFI_DEVICE_PATH_PROTOCOL*));
EFI_STATUS PrintNode(EFI_DEVICE_PATH_PROTOCOL * Node);


#endif