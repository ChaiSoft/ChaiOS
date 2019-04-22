#ifndef CHAIOS_UEFI_LIB_H
#define CHAIOS_UEFI_LIB_H

#include <Uefi.h>
#include "efigop.h"
#include <kernelinfo.h>

typedef void EFI_FILE;

EFI_STATUS InitializeLib(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);
void Stall(size_t microseconds);
EFI_BOOT_SERVICES* GetBootServices();
EFI_RUNTIME_SERVICES* GetRuntimeServices();
char16_t* getError(const EFI_STATUS error);
EFI_FILE* OpenFile(const char16_t* filename, const char* mode);
UINT64 GetFileSize(EFI_FILE* file);
EFI_STATUS CloseFile(EFI_FILE* file);
UINTN ReadFile(void* buffer, UINTN size, UINTN count, EFI_FILE* file);
UINTN WriteFile(void* buffer, UINTN size, UINTN count, EFI_FILE* file);
EFI_STATUS getErrno();
size_t GetIntegerInput(char16_t* prompt);
EFI_STATUS SetGraphicsMode(UINT32 Mode);
EFI_STATUS PrepareExitBootServices();
typedef bool(*gmode_callback)(UINT32 Mode, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info);
UINT32 IterateGraphicsMode(gmode_callback callback);	//Runs until true callback, returns UINT32_MAX if no match
EFI_STATUS GetGraphicsModeInfo(UINT32 Mode, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION** info, UINTN* SizeOfInfo);
EFI_STATUS SetWorkingDirectory(const char16_t* directory);
void get_framebuffer_info(PFRAMEBUFFER_INFORMATION& fbinfo);

int strcmp_basic(const char* s1, const char* s2);
int strcmp_nocase(const char* s1, const char* s2);

#endif
