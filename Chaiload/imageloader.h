#ifndef CHAIOS_IMAGELOADER_H
#define CHAIOS_IMAGELOADER_H

#include <stdheaders.h>
#include <kernelinfo.h>

typedef void* KLOAD_HANDLE;

KLOAD_HANDLE LoadImage(void* filebuf, const char16_t* filename);

kimage_entry GetEntryPoint(KLOAD_HANDLE image);
void* GetProcAddress(KLOAD_HANDLE image, const char* procname);
void fill_modloader_info(MODLOAD_INFO& binf);
size_t GetStackSize(KLOAD_HANDLE image);

#endif
