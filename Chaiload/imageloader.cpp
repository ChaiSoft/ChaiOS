#include "imageloader.h"
#include "uefilib.h"
#include "kterm.h"
#include "arch_paging.h"

#pragma pack(push, 1)
typedef struct _IMAGE_DOS_HEADER {  // DOS .EXE header
	uint16_t e_magic;		// must contain "MZ"
	uint16_t e_cblp;		// number of bytes on the last page of the file
	uint16_t e_cp;		// number of pages in file
	uint16_t e_crlc;		// relocations
	uint16_t e_cparhdr;		// size of the header in paragraphs
	uint16_t e_minalloc;	// minimum and maximum paragraphs to allocate
	uint16_t e_maxalloc;
	uint16_t e_ss;		// initial SS:SP to set by Loader
	uint16_t e_sp;
	uint16_t e_csum;		// checksum
	uint16_t e_ip;		// initial CS:IP
	uint16_t e_cs;
	uint16_t e_lfarlc;		// address of relocation table
	uint16_t e_ovno;		// overlay number
	uint16_t e_res[4];		// resevered
	uint16_t e_oemid;		// OEM id
	uint16_t e_oeminfo;		// OEM info
	uint16_t e_res2[10];	// reserved
	uint32_t   e_lfanew;	// address of new EXE header
} IMAGE_DOS_HEADER, *PIMAGE_DOS_HEADER;

typedef struct _IMAGE_NT_HEADERS_PE32 IMAGE_NT_HEADERS_PE32, *PIMAGE_NT_HEADERS_PE32;
typedef struct _IMAGE_NT_HEADERS_PE32PLUS IMAGE_NT_HEADERS_PE32PLUS, *PIMAGE_NT_HEADERS_PE32PLUS;

enum PeMachineType {
	IMAGE_FILE_MACHINE_AMD64 = 0x8664,
	IMAGE_FILE_MACHINE_ARM = 0x1C0,
	IMAGE_FILE_MACHINE_EBC = 0xEBC,
	IMAGE_FILE_MACHINE_I386 = 0x14c,
	IMAGE_FILE_MACHINE_THUMB = 0x1c2,
	IMAGE_FILE_MACHINE_ARM64 = 0xaa64,
	IMAGE_FILE_MACHINE_ARMNT = 0x1c4
};
typedef struct _IMAGE_FILE_HEADER {
	uint16_t  Machine;
	uint16_t  NumberOfSections;			// Number of sections in section table
	uint32_t   TimeDateStamp;			// Date and time of program link
	uint32_t   PointerToSymbolTable;		// RVA of symbol table
	uint32_t   NumberOfSymbols;			// Number of symbols in table
	uint16_t  SizeOfOptionalHeader;		// Size of IMAGE_OPTIONAL_HEADER in bytes
	uint16_t  Characteristics;
} IMAGE_FILE_HEADER, *PIMAGE_FILE_HEADER;

enum PeOptionalMagic {
	MAGIC_PE32 = 0x10b,
	MAGIC_PE32P = 0x20b
};

typedef struct _IMAGE_DATA_DIRECTORY {
	uint32_t VirtualAddress;		// RVA of table
	uint32_t Size;			// size of table
} IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;

#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16

typedef struct _IMAGE_OPTIONAL_HEADER_PE32 {

	uint16_t  Magic;				// not-so-magical number
	uint8_t   MajorLinkerVersion;			// linker version
	uint8_t   MinorLinkerVersion;
	uint32_t   SizeOfCode;				// size of .text in bytes
	uint32_t   SizeOfInitializedData;		// size of .bss (and others) in bytes
	uint32_t   SizeOfUninitializedData;		// size of .data,.sdata etc in bytes
	uint32_t   AddressOfEntryPoint;		// RVA of entry point
	uint32_t   BaseOfCode;				// base of .text
	uint32_t   BaseOfData;				// base of .data
	uint32_t   ImageBase;				// image base VA
	uint32_t   SectionAlignment;			// file section alignment
	uint32_t   FileAlignment;			// file alignment
	uint16_t  MajorOperatingSystemVersion;	// Windows specific. OS version required to run image
	uint16_t  MinorOperatingSystemVersion;
	uint16_t  MajorImageVersion;			// version of program
	uint16_t  MinorImageVersion;
	uint16_t  MajorSubsystemVersion;		// Windows specific. Version of SubSystem
	uint16_t  MinorSubsystemVersion;
	uint32_t   Reserved1;
	uint32_t   SizeOfImage;			// size of image in bytes
	uint32_t   SizeOfHeaders;			// size of headers (and stub program) in bytes
	uint32_t   CheckSum;				// checksum
	uint16_t  Subsystem;				// Windows specific. subsystem type
	uint16_t  DllCharacteristics;			// DLL properties
	uint32_t   SizeOfStackReserve;			// size of stack, in bytes
	uint32_t   SizeOfStackCommit;			// size of stack to commit
	uint32_t   SizeOfHeapReserve;			// size of heap, in bytes
	uint32_t   SizeOfHeapCommit;			// size of heap to commit
	uint32_t   LoaderFlags;			// no longer used
	uint32_t   NumberOfRvaAndSizes;		// number of DataDirectory entries
	IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER_PE32, *PIMAGE_OPTIONAL_HEADER_PE32;

typedef struct _IMAGE_OPTIONAL_HEADER_PE32PLUS {

	uint16_t  Magic;				// not-so-magical number
	uint8_t   MajorLinkerVersion;			// linker version
	uint8_t   MinorLinkerVersion;
	uint32_t   SizeOfCode;				// size of .text in bytes
	uint32_t   SizeOfInitializedData;		// size of .bss (and others) in bytes
	uint32_t   SizeOfUninitializedData;		// size of .data,.sdata etc in bytes
	uint32_t   AddressOfEntryPoint;		// RVA of entry point
	uint32_t   BaseOfCode;				// base of .text
	uint64_t   ImageBase;				// image base VA
	uint32_t   SectionAlignment;			// file section alignment
	uint32_t   FileAlignment;			// file alignment
	uint16_t  MajorOperatingSystemVersion;	// Windows specific. OS version required to run image
	uint16_t  MinorOperatingSystemVersion;
	uint16_t  MajorImageVersion;			// version of program
	uint16_t  MinorImageVersion;
	uint16_t  MajorSubsystemVersion;		// Windows specific. Version of SubSystem
	uint16_t  MinorSubsystemVersion;
	uint32_t   Reserved1;
	uint32_t   SizeOfImage;			// size of image in bytes
	uint32_t   SizeOfHeaders;			// size of headers (and stub program) in bytes
	uint32_t   CheckSum;				// checksum
	uint16_t  Subsystem;				// Windows specific. subsystem type
	uint16_t  DllCharacteristics;			// DLL properties
	uint64_t   SizeOfStackReserve;			// size of stack, in bytes
	uint64_t   SizeOfStackCommit;			// size of stack to commit
	uint64_t   SizeOfHeapReserve;			// size of heap, in bytes
	uint64_t   SizeOfHeapCommit;			// size of heap to commit
	uint32_t   LoaderFlags;			// no longer used
	uint32_t   NumberOfRvaAndSizes;		// number of DataDirectory entries
	IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER_PE32PLUS, *PIMAGE_OPTIONAL_HEADER_PE32PLUS;

struct _IMAGE_NT_HEADERS_PE32 {
	uint32_t                 Signature;
	IMAGE_FILE_HEADER     FileHeader;
	IMAGE_OPTIONAL_HEADER_PE32 OptionalHeader;
};

struct _IMAGE_NT_HEADERS_PE32PLUS {
	uint32_t                 Signature;
	IMAGE_FILE_HEADER     FileHeader;
	IMAGE_OPTIONAL_HEADER_PE32PLUS OptionalHeader;
};

#define IMAGE_SCN_CNT_CODE 0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080
#define IMAGE_SCN_MEM_DISCARDABLE 0x02000000
#define IMAGE_SCN_MEM_NOT_CACHED 0x04000000
#define IMAGE_SCN_MEM_NOT_PAGED 0x08000000
#define IMAGE_SCN_MEM_SHARED 0x10000000
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_READ 0x40000000
#define IMAGE_SCN_MEM_WRITE 0x80000000

typedef struct _IMAGE_SECTION_HEADER {
	char Name[8];
	uint32_t VirtualSize;
	uint32_t VirtualAddress;
	uint32_t SizeOfRawData;
	uint32_t PointerToRawData;
	uint32_t PointerToRelocations;
	uint32_t PointerToLinenumbers;
	uint16_t NumberOfRelocations;
	uint16_t NumberOfLinenumbers;
	uint32_t Characteristics;
}SECTION_HEADER,*PSECTION_HEADER;

#define IMAGE_DATA_DIRECTORY_EXPORT 0
#define IMAGE_DATA_DIRECTORY_IMPORT 1

typedef struct _IMAGE_EXPORT_DIRECTORY {
	uint32_t ExportFlags;
	uint32_t TimeDateStamp;
	uint16_t MajorVersion;
	uint16_t MinorVersion;
	uint32_t NameRva;
	uint32_t OrdinalBase;
	uint32_t AddressTableEntries;
	uint32_t NumberOfNamePointers;
	uint32_t ExportTableRva;
	uint32_t NamePointerRva;
	uint32_t OrdinalTableRva;
}IMAGE_EXPORT_DIRECTORY, *PIMAGE_EXPORT_DIRECTORY;

typedef struct _IMAGE_IMPORT_DIRECTORY {
	uint32_t ImportLookupRva;
	uint32_t TimeDateStamp;
	uint32_t ForwarderChain;
	uint32_t NameRva;
	uint32_t ThunkTableRva;
}IMAGE_IMPORT_DIRECTORY, *PIMAGE_IMPORT_DIRECTORY;

typedef struct _IMAGE_IMPORT_HINT_TABLE {
	uint16_t Hint;
	char name[2];
}IMAGE_IMPORT_HINT_TABLE, *PIMAGE_IMPORT_HINT_TABLE;


#define IMAGE_IMPORT_LOOKUP_TABLE_FLAG_PE32 0x80000000
typedef uint32_t IMAGE_IMPORT_LOOKUP_TABLE_PE32, *PIMAGE_IMPORT_LOOKUP_TABLE_PE32;

#define IMAGE_IMPORT_LOOKUP_TABLE_FLAG_PE32P 0x8000000000000000
typedef uint64_t IMAGE_IMPORT_LOOKUP_TABLE_PE32P, *PIMAGE_IMPORT_LOOKUP_TABLE_PE32P;

#pragma pack(pop)

#ifdef X86
typedef struct  _IMAGE_NT_HEADERS_PE32 IMAGE_NT_HEADERS, *PIMAGE_NT_HEADERS;
typedef IMAGE_IMPORT_LOOKUP_TABLE_PE32 IMAGE_IMPORT_LOOKUP_TABLE, *PIMAGE_IMPORT_LOOKUP_TABLE;
static const PeOptionalMagic MAGIC_NATIVE = PeOptionalMagic::MAGIC_PE32;
static const PeMachineType MACHINE_NATIVE = IMAGE_FILE_MACHINE_I386;
#define IMAGE_IMPORT_LOOKUP_TABLE_FLAG IMAGE_IMPORT_LOOKUP_TABLE_FLAG_PE32
#elif defined X64
typedef struct  _IMAGE_NT_HEADERS_PE32PLUS IMAGE_NT_HEADERS, *PIMAGE_NT_HEADERS;
typedef IMAGE_IMPORT_LOOKUP_TABLE_PE32P IMAGE_IMPORT_LOOKUP_TABLE, *PIMAGE_IMPORT_LOOKUP_TABLE;
static const PeOptionalMagic MAGIC_NATIVE = PeOptionalMagic::MAGIC_PE32P;
static const PeMachineType MACHINE_NATIVE = IMAGE_FILE_MACHINE_AMD64;
#define IMAGE_IMPORT_LOOKUP_TABLE_FLAG IMAGE_IMPORT_LOOKUP_TABLE_FLAG_PE32P
#else
#error "Unknown machine architecture"
#endif

static void copy_mem(void* dst, void* src, size_t length)
{
	uint8_t* dstp = (uint8_t*)dst;
	uint8_t* srcp = (uint8_t*)src;
	while (length--)
		*dstp++ = *srcp++;
}

static void zero_mem(void* dst, size_t length)
{
	uint8_t* dstp = (uint8_t*)dst;
	while (length--)
		*dstp++ = 0;
}

static PIMAGE_DESCRIPTOR loaded_images = nullptr;

static PIMAGE_DESCRIPTOR iterate_images(PIMAGE_DESCRIPTOR last)
{
	if (!last)
		return loaded_images;
	else
		return last->next;
}

static void register_image(void* imaddr, const char* filename, void(*entry)(void*), ChaiosBootType type)
{
	PIMAGE_DESCRIPTOR* imgs = &loaded_images;
	while (*imgs)
		imgs = &(*imgs)->next;
	PIMAGE_DESCRIPTOR cur = *imgs = new IMAGE_DESCRIPTOR;
	cur->filename = filename;
	cur->location = imaddr;
	cur->imageType = type;
	cur->EntryPoint = entry;
	cur->next = nullptr;
}

static size_t binary_search_strings(const void* ImageBase, const uint32_t* list, const size_t length, const char* value)
{
	size_t begin = 0;
	size_t mid = length / 2;
	size_t end = length;
	while (end - begin > 0)
	{
		const char* teststr = raw_offset<const char*>(ImageBase, list[mid]);
		int compare = strcmp_basic(teststr, value);
		if (compare == 0)
			return mid;
		else if (compare < 0)
		{
			//Further on
			begin = mid + 1;
			mid = mid + (end - mid) / 2;
		}
		else if (compare > 0)
		{
			//Further back
			end = mid;
			mid = begin + (mid-begin) / 2;
		}
	}
	return length;
}

void* GetProcAddress(KLOAD_HANDLE image, const char* procname)
{
	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)image;
	PIMAGE_NT_HEADERS ntHeaders = raw_offset<PIMAGE_NT_HEADERS>(dosHeader, dosHeader->e_lfanew);
	if (IMAGE_DATA_DIRECTORY_EXPORT + 1 > ntHeaders->OptionalHeader.NumberOfRvaAndSizes)
		return nullptr;
	IMAGE_DATA_DIRECTORY& datadir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_EXPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0)
		return nullptr;
	PIMAGE_EXPORT_DIRECTORY exportdir = raw_offset<PIMAGE_EXPORT_DIRECTORY>(image, datadir.VirtualAddress);
	//Find procedure with given name. Binary search
	const uint32_t* nameptrtable = raw_offset<const uint32_t*>(image, exportdir->NamePointerRva);
	size_t index = binary_search_strings(image, nameptrtable, exportdir->NumberOfNamePointers, procname);
	if (index == exportdir->NumberOfNamePointers)
		return nullptr;
	uint16_t ordinal = raw_offset<uint16_t*>(image, exportdir->OrdinalTableRva)[index] + exportdir->OrdinalBase;
	uint32_t ProcRva = raw_offset<uint32_t*>(image, exportdir->ExportTableRva)[ordinal - exportdir->OrdinalBase];
	return raw_offset<void*>(image, ProcRva);
}

const char* GetDllName(KLOAD_HANDLE image)
{
	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)image;
	PIMAGE_NT_HEADERS ntHeaders = raw_offset<PIMAGE_NT_HEADERS>(dosHeader, dosHeader->e_lfanew);
	if (IMAGE_DATA_DIRECTORY_EXPORT + 1 > ntHeaders->OptionalHeader.NumberOfRvaAndSizes)
		return nullptr;
	IMAGE_DATA_DIRECTORY& datadir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_EXPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0)
		return nullptr;
	PIMAGE_EXPORT_DIRECTORY exportdir = raw_offset<PIMAGE_EXPORT_DIRECTORY>(image, datadir.VirtualAddress);
	return raw_offset<const char*>(image, exportdir->NameRva);
}

static void link_image_import(KLOAD_HANDLE importer, KLOAD_HANDLE exporter)
{
	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)importer;
	PIMAGE_NT_HEADERS ntHeaders = raw_offset<PIMAGE_NT_HEADERS>(dosHeader, dosHeader->e_lfanew);
	if (IMAGE_DATA_DIRECTORY_IMPORT + 1 > ntHeaders->OptionalHeader.NumberOfRvaAndSizes)
		return;
	IMAGE_DATA_DIRECTORY& datadir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_IMPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0)
		return;
	PIMAGE_IMPORT_DIRECTORY importdir = raw_offset<PIMAGE_IMPORT_DIRECTORY>(importer, datadir.VirtualAddress);
	for (size_t n = 0; importdir[n].ThunkTableRva; ++n)
	{
		const char* thisdll = raw_offset<const char*>(importer, importdir[n].NameRva);
		if (strcmp_basic(thisdll, GetDllName(exporter)) != 0)
			continue;
		PIMAGE_IMPORT_LOOKUP_TABLE iat = raw_offset<PIMAGE_IMPORT_LOOKUP_TABLE>(importer, importdir[n].ThunkTableRva);
		while (*iat)
		{
			if (*iat & IMAGE_IMPORT_LOOKUP_TABLE_FLAG)
			{
				//ordinal
				uint16_t ordinal = *iat & ~IMAGE_IMPORT_LOOKUP_TABLE_FLAG;
				printf(u"Error: loading by ordinal (%d) unspported\n", ordinal);
			}
			else
			{
				PIMAGE_IMPORT_HINT_TABLE hint = raw_offset<PIMAGE_IMPORT_HINT_TABLE>(importer, *iat);
				const char* fname = hint->name;
				void* procaddr = GetProcAddress(exporter, fname);
				if (!procaddr)
					printf(u"Error importing function %S from DLL %S\n", fname, thisdll);
				*iat = (IMAGE_IMPORT_LOOKUP_TABLE)procaddr;
			}
			++iat;
		}
	}
}

static void link_image_imports(KLOAD_HANDLE image)
{
	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)image;
	PIMAGE_NT_HEADERS ntHeaders = raw_offset<PIMAGE_NT_HEADERS>(dosHeader, dosHeader->e_lfanew);
	if (IMAGE_DATA_DIRECTORY_IMPORT + 1 > ntHeaders->OptionalHeader.NumberOfRvaAndSizes)
		return;
	IMAGE_DATA_DIRECTORY& datadir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DATA_DIRECTORY_IMPORT];
	if (datadir.VirtualAddress == 0 || datadir.Size == 0)
		return;
	PIMAGE_IMPORT_DIRECTORY importdir = raw_offset<PIMAGE_IMPORT_DIRECTORY>(image, datadir.VirtualAddress);
	for (size_t n = 0; importdir[n].ThunkTableRva; ++n)
	{
		const char* thisdll = raw_offset<const char*>(image, importdir[n].NameRva);
		PIMAGE_DESCRIPTOR dlld = nullptr;
		while (dlld = iterate_images(dlld))
		{
			if (strcmp_nocase(dlld->filename, thisdll) == 0)
				break;
		}
		if (!dlld)
			continue;
		KLOAD_HANDLE hdll = dlld->location;
		PIMAGE_IMPORT_LOOKUP_TABLE iat = raw_offset<PIMAGE_IMPORT_LOOKUP_TABLE>(image, importdir[n].ThunkTableRva);
		while (*iat)
		{
			if (*iat & IMAGE_IMPORT_LOOKUP_TABLE_FLAG)
			{
				//ordinal
				uint16_t ordinal = *iat & ~IMAGE_IMPORT_LOOKUP_TABLE_FLAG;
				printf(u"Error: loading by ordinal (%d) unspported\n", ordinal);
			}
			else
			{
				PIMAGE_IMPORT_HINT_TABLE hint = raw_offset<PIMAGE_IMPORT_HINT_TABLE>(image, *iat);
				const char* fname = hint->name;
				void* procaddr = GetProcAddress(hdll, fname);
				if (!procaddr)
					printf(u"Error importing function %S from DLL %S\n", fname, thisdll);
				*iat = (IMAGE_IMPORT_LOOKUP_TABLE)procaddr;
			}
			++iat;
		}
	}
}

static void link_image(KLOAD_HANDLE image)
{
	//Our dllname
	const char* dllname = GetDllName(image);
	//Fix up imports
	link_image_imports(image);
	//Iterate through loaded images and offer exports
	PIMAGE_DESCRIPTOR current = nullptr;
	while (current = iterate_images(current))
	{
		link_image_import(current->location, image);
	}
}

size_t GetStackSize(KLOAD_HANDLE image)
{
	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)image;
	PIMAGE_NT_HEADERS ntHeaders = raw_offset<PIMAGE_NT_HEADERS>(dosHeader, dosHeader->e_lfanew);
	return ntHeaders->OptionalHeader.SizeOfStackReserve;
}

KLOAD_HANDLE LoadImage(void* filebuf, const char16_t* filename, ChaiosBootType imageType)
{
	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)filebuf;
	PIMAGE_NT_HEADERS ntHeaders = raw_offset<PIMAGE_NT_HEADERS>(dosHeader, dosHeader->e_lfanew);
	if (ntHeaders->FileHeader.Machine != MACHINE_NATIVE || ntHeaders->OptionalHeader.Magic != MAGIC_NATIVE)
		return NULL;
	//Now load the image sections
	PSECTION_HEADER sectionHeaders = raw_offset<PSECTION_HEADER>(&ntHeaders->OptionalHeader, ntHeaders->FileHeader.SizeOfOptionalHeader);
	size_t ImageBase = ntHeaders->OptionalHeader.ImageBase;
	void* ImBase = (void*)ImageBase;
	//Check if we need to relocate
	if (!check_free(ImBase, ntHeaders->OptionalHeader.SizeOfImage))
	{
		printf(u"Error: cannot load image %s at address %x: cannot relocate\n", filename, ImageBase);
		return NULL;
	}
	//Start loading the image to memory
	if (!paging_map(ImBase, PADDR_T_MAX, ntHeaders->OptionalHeader.SizeOfHeaders, PAGE_ATTRIBUTE_WRITABLE))
	{
		printf(u"Error: cannot load image %s, mapping headers failed\n", filename);
		return NULL;
	}
	copy_mem(ImBase, filebuf, ntHeaders->OptionalHeader.SizeOfHeaders);

	for (size_t i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i)
	{
		char16_t buf[9];
		copy_mem(buf, sectionHeaders[i].Name, 8);
		buf[8] = 0;
		size_t load_addr = ImageBase + sectionHeaders[i].VirtualAddress;
		void* sectaddr = (void*)load_addr;
		size_t sectsize = sectionHeaders[i].VirtualSize;
		if (!paging_map((void*)load_addr, PADDR_T_MAX, sectsize, PAGE_ATTRIBUTE_WRITABLE))
		{
			printf(u"Failed to map section %S\n", buf);
			return nullptr;
		}
		copy_mem(sectaddr, raw_offset<void*>(filebuf, sectionHeaders[i].PointerToRawData), sectionHeaders[i].SizeOfRawData);
		if(sectionHeaders[i].VirtualSize > sectionHeaders[i].SizeOfRawData)
			zero_mem(raw_offset<void*>(sectaddr, sectionHeaders[i].SizeOfRawData), sectionHeaders[i].VirtualSize - sectionHeaders[i].SizeOfRawData);	//Per spec, zero fill rest
		if ((sectionHeaders[i].Characteristics & IMAGE_SCN_MEM_WRITE) == 0)
			set_paging_attributes(sectaddr, sectsize, 0, PAGE_ATTRIBUTE_WRITABLE);		//Clear writable now it's written
		if ((sectionHeaders[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
			set_paging_attributes(sectaddr, sectsize, PAGE_ATTRIBUTE_NO_EXECUTE, 0);
		if((sectionHeaders[i].Characteristics & IMAGE_SCN_MEM_NOT_PAGED) == 0)
			set_paging_attributes(sectaddr, sectsize, PAGE_ATTRIBUTE_NO_PAGING, 0);
		if ((sectionHeaders[i].Characteristics & IMAGE_SCN_MEM_NOT_CACHED) == 0)
			set_paging_attributes(sectaddr, sectsize, PAGE_ATTRIBUTE_NO_CACHING, 0);
	}
	//Sections are now loaded into memory. Register the image and invoke the dynamic linker
	link_image(ImBase);
	const char* dllname = GetDllName(ImBase);
	if (!dllname)
	{
		char* buffer = new char[256];
		int i = 0;
		for (; filename[i] && i < 255; ++i)
			buffer[i] = filename[i];
		buffer[i] = 0;
		dllname = buffer;
	}
	register_image(ImBase, dllname, (void(*)(void*))GetEntryPoint(ImBase), imageType);

	return ImBase;
}

kimage_entry GetEntryPoint(KLOAD_HANDLE image)
{
	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)image;
	PIMAGE_NT_HEADERS ntHeaders = raw_offset<PIMAGE_NT_HEADERS>(dosHeader, dosHeader->e_lfanew);
	if (ntHeaders->FileHeader.Machine != MACHINE_NATIVE || ntHeaders->OptionalHeader.Magic != MAGIC_NATIVE)
		return nullptr;
	return raw_offset<kimage_entry>(image, ntHeaders->OptionalHeader.AddressOfEntryPoint);
}

static struct {
	PIMAGE_DESCRIPTOR image_list;
}modloader_info;

void fill_modloader_info(MODLOAD_INFO& binf)
{
	modloader_info.image_list = loaded_images;
	binf = &modloader_info;
}