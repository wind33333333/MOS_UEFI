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
#include <Guid/Acpi.h>

#define KERNELSTARTADDR 0x100000

EFI_STATUS EFIAPI keyCountdown (IN UINT32 Times);
EFI_STATUS EFIAPI PrintInput (IN OUT CHAR16* InputBuffer,IN OUT UINT32* InputBufferLength);
EFI_DEVICE_PATH_PROTOCOL* WalkthroughDevicePath(EFI_DEVICE_PATH_PROTOCOL* DevPath, EFI_STATUS (*Callbk)(EFI_DEVICE_PATH_PROTOCOL*));
EFI_STATUS PrintNode(EFI_DEVICE_PATH_PROTOCOL * Node);

typedef struct{
    /*显卡信息*/
    UINT32* FrameBufferBase;
    UINT32  HorizontalResolution;
    UINT32  VerticalResolution;
    UINT64  FrameBufferSize;

    /*内存图*/
    EFI_MEMORY_DESCRIPTOR* MemoryMap;

    /*ACPI*/

}BootInfo;


#endif