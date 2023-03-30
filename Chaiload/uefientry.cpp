#include <Uefi.h>
#include "kterm.h"
#include "kdraw.h"
#include "uefilib.h"
#include "liballoc.h"
#include "assembly.h"
#include "imageloader.h"
#include "inih.h"
#include "pmmngr.h"
#include "arch_paging.h"
#include <kernelinfo.h>


extern bool CallConstructors();

enum StrchrMode {
	FROM_BEGIN,
	FROM_END
};
static const char16_t* wstrchr(const char16_t* str, char16_t c, StrchrMode mode = FROM_BEGIN)
{
	size_t i = 0;
	size_t temp = 0;
search_loop:
	while (str[i] && str[i] != c) ++i;
	if (mode == FROM_END)
	{
		if (str[i])
		{
			temp = i++;
			goto search_loop;
		}
		else
			return &str[temp];
	}
	return &str[i];
}

typedef struct _chaios_boot_files {
	const ChaiosBootType bootType;
	const char16_t* fileName;
	void* loadLocation;
	size_t fileSize;
	_chaios_boot_files* next;
}CHAIOS_BOOT_FILES, *PCHAIOS_BOOT_FILES;

static CHAIOS_BOOT_FILES _config = {
	CHAIOS_BOOT_CONFIGURATION,
	u"chaiload.ini",
	nullptr,
	0,
	nullptr
};

static CHAIOS_BOOT_FILES _kernel = {
	CHAIOS_BOOT_KERNEL,
	u"chaikrnl.exe",
	nullptr,
	0,
	&_config
};

static CHAIOS_BOOT_FILES _acpica = {
	CHAIOS_DLL,
	u"acpica.dll",
	nullptr,
	0,
	&_kernel
};

static CHAIOS_BOOT_FILES bootfiles = {
	CHAIOS_DLL,
	u"kcstdlib.dll",
	nullptr,
	0,
	&_acpica
};

static CHAIOS_BOOT_FILES* last_bootfile = &_config;

static CHAIOS_BOOT_FILES* iterateBootFiles(CHAIOS_BOOT_FILES* last)
{
	if (!last)
		return &bootfiles;
	else
		return last->next;
}

static size_t graphics_width = 0;
static size_t graphics_height = 0;

static const char* chars = "0123456789ABCDEF";
static size_t atoi_basic(const char* str, int base = 10)
{
	if (base < 2 || base > 16)
		return 0;
	size_t value = 0;
	bool negative = false;
	if (*str == '-')
	{
		negative = true; 
		++str;
	}
	while (*str)
	{
		value *= base;
		size_t i = 0;
		for (; i < 16; ++i)
			if (chars[i] == *str) break;
		if (i == 16)
			return 0;
		value += i;
		++str;
	}
	if (negative)
		return -value;
	else
		return value;
}

static int bootini_handler(void* user, const char* section, const char* name, const char* value)
{
	if (strcmp_basic(section, "graphics") == 0)
	{
		if (strcmp_basic(name, "width") == 0)
		{
			graphics_width = atoi_basic(value);
		}
		else if (strcmp_basic(name, "height") == 0)
		{
			graphics_height = atoi_basic(value);
		}
	}
	else if (strcmp_basic(section, "drivers") == 0)
	{
		puts(u"Loading driver\n");
		CHAIOS_BOOT_FILES archetype = { CHAIOS_BOOT_DRIVER };
		PCHAIOS_BOOT_FILES newentry = new CHAIOS_BOOT_FILES(archetype);
		size_t vallength = 0;
		for (; value[vallength]; ++vallength);
		char16_t* buffer = new char16_t[vallength + 1];
		for (int i = 0; i < vallength; ++i)
			buffer[i] = value[i];
		buffer[vallength] = 0;
		newentry->fileName = buffer;
		newentry->fileSize = 0;
		newentry->loadLocation = nullptr;
		newentry->next = nullptr;
		last_bootfile->next = newentry;
		last_bootfile = newentry;
	}
	return 1;
}

static bool print_graphics_mode(UINT32 Mode, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info)
{
	printf(u"Mode %d: %dx%d\r\n", Mode, info->HorizontalResolution, info->VerticalResolution);
	return false;
}
static bool match_config_resoultion(UINT32 Mode, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info)
{
	return info->HorizontalResolution == graphics_width && info->VerticalResolution == graphics_height;
}

#ifdef X64
void* stackaddr = (void*)0xFFFFE00000000000;
void* heapaddr = (void*)0xFFFFD80000000000;
#else
#error "Unknown architecture"
#endif
static void* arch_allocate_pages(size_t numPages)
{
	if (!paging_map(heapaddr, PADDR_T_MAX, numPages*PAGESIZE, PAGE_ATTRIBUTE_WRITABLE))
		return nullptr;
	void* ret = heapaddr;
	heapaddr = raw_offset<void*>(heapaddr, numPages*PAGESIZE);
	return ret;
}

static int arch_free_pages(void* p, size_t numpages)
{
	return 0;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	InitializeLib(ImageHandle, SystemTable);
	CallConstructors();

	EFI_STATUS Status;

	const char16_t* searchdir = u"\\EFI\\ChaiOS\\";
	if (Status = SetWorkingDirectory(searchdir))
		printf(u"Error setting working directory: %s\r\n", getError(Status));

	CHAIOS_BOOT_FILES* bootfile = nullptr;
	while (bootfile = iterateBootFiles(bootfile))
	{
		if (bootfile->loadLocation != nullptr)
			continue;		//Support in-memory images

		EFI_FILE* file = OpenFile(bootfile->fileName, "r");
		if (!file)
		{
			printf(u"Error: could not open %s: %s\r\n", bootfile->fileName, getError(getErrno()));
			continue;
		}
		UINT64 fileSize = GetFileSize(file);
		VOID* bootfilebuf = kmalloc(fileSize+1);
		UINTN read = ReadFile(bootfilebuf, 1, fileSize, file);
		if (read < fileSize)
		{
			CloseFile(file);
			printf(u"Read %d bytes, failed\r\n", read);
			continue;
		}
		else
			printf(u"Successfully read %d bytes\r\n", read);
		//Boot file is now loaded into memory
		CloseFile(file);
		bootfile->loadLocation = bootfilebuf;
		bootfile->fileSize = fileSize;
		if (bootfile->bootType == CHAIOS_BOOT_CONFIGURATION)
		{
			//We need to parse this now. INI format
			((char*)bootfilebuf)[fileSize] = '\0';
			ini_parse_string((const char*)bootfilebuf, &bootini_handler, nullptr);
		}
	}
	//size_t value = GetIntegerInput(u"Enter scrolling lines configuration: ");
	//set_scrolllines(value);
	UINT32 AutoMode = IterateGraphicsMode(&match_config_resoultion);
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info; UINTN SizeOfInfo;
	if (AutoMode == UINT32_MAX)
	{
		if (!EFI_ERROR(GetGraphicsModeInfo(AutoMode, &info, &SizeOfInfo)))
		{
			IterateGraphicsMode(&print_graphics_mode);
			size_t value = GetIntegerInput(u"Enter boot graphics mode: ");
			SetGraphicsMode(value);
			AutoMode = value;
		}
	}
	else
	{
		SetGraphicsMode(AutoMode);
	}
	if (!EFI_ERROR(GetGraphicsModeInfo(AutoMode, &info, &SizeOfInfo)))
	{
		printf(u"Graphics mode %d: %dx%d\r\n", AutoMode, info->HorizontalResolution, info->VerticalResolution);
	}

	puts(u"ChaiOS 0.09 UEFI Loader\r\n");
	int majorver = SystemTable->Hdr.Revision / (1 << 16);
	int minorver = SystemTable->Hdr.Revision % (1 << 16);
	printf(u"Firmware Vendor: %s, version: %d (UEFI:%d.%d)\r\n", SystemTable->FirmwareVendor, SystemTable->FirmwareRevision, majorver, minorver);
	//Read ACPI configuration tables
	//startup_acpi(SystemTable);
	//startup_multiprocessor();

	const size_t EARLY_PAGE_STACK_SIZE = 1024*1024;
	EFI_PHYSICAL_ADDRESS earlyPhypageStack = 0;
	if (EFI_ERROR(SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, EARLY_PAGE_STACK_SIZE / EFI_PAGE_SIZE, &earlyPhypageStack)))
	{
		puts(u"Could not allocate page stack\r\n");
		return EFI_OUT_OF_RESOURCES;
	}

	SystemTable->BootServices->SetWatchdogTimer(0, 0, 0, nullptr);

	PrepareExitBootServices();

	EfiMemoryMap map;
	map.MemMapSize = map.MapKey = map.DescriptorSize = map.DescriptorVersion = 0;

	SystemTable->BootServices->GetMemoryMap(&map.MemMapSize, nullptr, &map.MapKey, &map.DescriptorSize, &map.DescriptorVersion);
	//Give a nice bit of room to spare (memory map can change)
	map.MemMapSize += 16 * map.DescriptorSize;
	map.memmap = (EFI_MEMORY_DESCRIPTOR*)kmalloc(map.MemMapSize);	//Allocate a nice buffer

	SystemTable->BootServices->GetMemoryMap(&map.MemMapSize, map.memmap, &map.MapKey, &map.DescriptorSize, &map.DescriptorVersion);
	printf(u"EFI Memory Map: Descriptor size %d\n", map.DescriptorSize);
#if 0
	//Dump the UEFI memory map to a file for testing
	EFI_FILE* file = OpenFile(u"efimap.dat", "w");
	if (!file)
	{
		printf(u"Error: could not open %s: %s\r\n", u"efimap.dat", getError(getErrno()));
	}
	WriteFile(map.memmap, 1, map.MemMapSize, file);
	CloseFile(file);
#endif
	if (EFI_ERROR(Status = SystemTable->BootServices->ExitBootServices(ImageHandle, map.MapKey)))
	{
		printf(u"Failed to exit boot services: %s\r\n", getError(Status));
		UINTN index;
		SystemTable->BootServices->WaitForEvent(1, &SystemTable->ConIn->WaitForKey, &index);
		return EFI_SUCCESS;
	}
	//We need to take control of the hardware now. Setup basic memory management
	setLiballocAllocator(nullptr, nullptr);

	InitializePmmngr(map, (void*)earlyPhypageStack, EARLY_PAGE_STACK_SIZE);
	puts(u"Physical memory manager intialized\n");
	arch_initialize_paging();
	puts(u"Paging initialized\n");
	setLiballocAllocator(&arch_allocate_pages, &arch_free_pages);
	//Now load the OS!
	bootfile = nullptr;
	kimage_entry kentry = nullptr;
	KLOAD_HANDLE kernel = NULL;
	while (bootfile = iterateBootFiles(bootfile))
	{
		printf(u"Boot file: %s @ %x, length %d, type %d\n", bootfile->fileName, bootfile->loadLocation, bootfile->fileSize, bootfile->bootType);
		if (!bootfile->loadLocation)
			continue;
		if (bootfile->bootType == CHAIOS_DLL || bootfile->bootType == CHAIOS_BOOT_DRIVER)
		{
			KLOAD_HANDLE dll = LoadImage(bootfile->loadLocation, bootfile->fileName, bootfile->bootType);
			if (bootfile->bootType == CHAIOS_DLL && GetProcAddress(dll, "memcpy"))
			{
				set_memcpy((memcpy_proc)GetProcAddress(dll, "memcpy"));
			}
			
		}
		else if (bootfile->bootType == CHAIOS_BOOT_KERNEL)
		{
			kernel = LoadImage(bootfile->loadLocation, bootfile->fileName, bootfile->bootType);
			kentry = GetEntryPoint(kernel);
		}
	}

	size_t kstacksize = GetStackSize(kernel);

	if (!paging_map(stackaddr, PADDR_T_MAX, kstacksize, PAGE_ATTRIBUTE_WRITABLE))
	{
		puts(u"Error: could not allocate kernel stack\n");
		while (1);
	}

	KERNEL_BOOT_INFO bootinfo;
	fill_pmmngr_info(bootinfo.pmmngr_info);
	fill_arch_paging_info(bootinfo.paging_info);
	fill_modloader_info(bootinfo.modloader_info);
	get_framebuffer_info(bootinfo.fbinfo);
	populate_kterm_info(bootinfo.kterm_status);
	bootinfo.efi_system_table = SystemTable;
	bootinfo.memory_map = &map;
	bootinfo.loaded_files = &bootfiles;
	bootinfo.boottype = CHAIOS_BOOT_TYPE_UEFI;
	bootinfo.printf_proc = &printf;
	bootinfo.puts_proc = &puts;
	bootinfo.bootstack = stackaddr;
	printf(u"Success: Kernel entry point at %x, stack at %x, length %x\n", kentry, stackaddr, kstacksize);
	call_kernel(&bootinfo, kentry, stackaddr, kstacksize);
	puts(u"Kernel returned");
	while (1);
}