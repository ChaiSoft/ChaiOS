#ifndef CHAIOS_UEFI_LIB_H
#define CHAIOS_UEFI_LIB_H

#include <Uefi.h>

EFI_STATUS InitializeLib(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);
void Stall(size_t microseconds);
EFI_BOOT_SERVICES* GetBootServices();
EFI_RUNTIME_SERVICES* GetRuntimeServices();

#endif
