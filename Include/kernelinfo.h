#ifndef CHAIOS_KERNEL_INFO_H
#define CHAIOS_KERNEL_INFO_H

#include <stdheaders.h>

typedef void* PMMNGR_INFO;
typedef void* PAGING_INFO;
typedef void* MODLOAD_INFO;

enum BootType {
	CHAIOS_BOOT_TYPE_UEFI
};

#pragma pack(push, 1)

typedef struct _FRAMEBUFFER_INFORMATION {
	void* phyaddr;
	size_t size;
	size_t pixelsPerLine;
	size_t Horizontal;
	size_t Vertical;
	uint32_t redmask;
	uint32_t greenmask;
	uint32_t bluemask;
	uint32_t resvmask;
}FRAMEBUFFER_INFORMATION, *PFRAMEBUFFER_INFORMATION;

typedef struct _KERNEL_BOOT_INFO {
	BootType boottype;
	PMMNGR_INFO pmmngr_info;
	void* efi_system_table;
	PAGING_INFO paging_info;
	void* memory_map;
	MODLOAD_INFO modloader_info;
	void* loaded_files;
	PFRAMEBUFFER_INFORMATION fbinfo;
	void(*puts_proc)(const char16_t*);
	void(*printf_proc)(const char16_t*, ...);
}KERNEL_BOOT_INFO, *PKERNEL_BOOT_INFO;

#pragma pack(pop)

typedef void(*kimage_entry)(PKERNEL_BOOT_INFO);

#endif
