#include "uefilib.h"
#include "kterm.h"
#include "liballoc.h"

static EFI_SYSTEM_TABLE* ST = nullptr;
static EFI_BOOT_SERVICES* BS = nullptr;
static EFI_RUNTIME_SERVICES* RS = nullptr;

static void uefi_puts(const char16_t* p)
{
	ST->ConOut->OutputString(ST->ConOut, (CHAR16*)p);
}
static void* uefi_alloc(size_t npages)
{
	EFI_PHYSICAL_ADDRESS phy = 0;
	BS->AllocatePages(AllocateAnyPages, EfiLoaderData, npages, &phy);
	return (void*)phy;
}
static int uefi_free(void* ptr, size_t npages)
{
	return BS->FreePages((EFI_PHYSICAL_ADDRESS)ptr, npages);
}

#define EFI_LOADED_IMAGE_PROTOCOL_GUID {0x5B1B31A1,0x9562,0x11d2,\
{0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}}

typedef struct {
	UINT32            Revision;
	EFI_HANDLE        ParentHandle;
	EFI_SYSTEM_TABLE  *SystemTable;
	EFI_HANDLE        DeviceHandle;
	EFI_DEVICE_PATH_PROTOCOL  *FilePath;
	VOID              *Reserved;
	UINT32            LoadOptionsSize;
	VOID              *LoadOptions;
	VOID              *ImageBase;
	UINT64            ImageSize;
	EFI_MEMORY_TYPE   ImageCodeType;
	EFI_MEMORY_TYPE   ImageDataType;
	EFI_IMAGE_UNLOAD  Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

EFI_STATUS InitializeLib(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	ST = SystemTable;
	BS = SystemTable->BootServices;
	RS = SystemTable->RuntimeServices;
	setKtermOutputProc(&uefi_puts);
	setLiballocAllocator(&uefi_alloc, &uefi_free);
	//Get boot information
	EFI_STATUS Status;
	EFI_GUID loadedimageprot = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_LOADED_IMAGE_PROTOCOL* loadedimage = nullptr;
	if (Status = BS->HandleProtocol(ImageHandle, &loadedimageprot, (void**)&loadedimage))
	{
		puts(u"Could not get boot information\n");
		return Status;
	}
	return EFI_SUCCESS;
}

void Stall(size_t microseconds)
{
	BS->Stall(microseconds);
}

EFI_BOOT_SERVICES* GetBootServices()
{
	return BS;
}
EFI_RUNTIME_SERVICES* GetRuntimeServices()
{
	return RS;
}