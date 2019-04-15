#ifndef CHAIOS_BOOT_PMMNGR_H
#define CHAIOS_BOOT_PMMNGR_H

#include <stdheaders.h>
#include <Uefi.h>
#include <kernelinfo.h>

#define PAGESIZE 4096

typedef EFI_PHYSICAL_ADDRESS paddr_t;
#define PADDR_T_MAX UINT64_MAX

struct EfiMemoryMap {
	EFI_MEMORY_DESCRIPTOR* memmap;
	UINTN MemMapSize, MapKey, DescriptorSize;
	UINT32 DescriptorVersion;
};

void InitializePmmngr(const EfiMemoryMap& memmap, void* buffer, size_t bufsize);
paddr_t PmmngrAllocate();
void PmmngrFree(paddr_t addr);
void fill_pmmngr_info(PMMNGR_INFO& binf);

#endif
