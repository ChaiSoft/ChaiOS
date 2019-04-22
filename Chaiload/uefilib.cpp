#include "uefilib.h"
#include "kterm.h"
#include "liballoc.h"
#include "efigop.h"
#include "kdraw.h"

static EFI_SYSTEM_TABLE* ST = nullptr;
static EFI_BOOT_SERVICES* BS = nullptr;
static EFI_RUNTIME_SERVICES* RS = nullptr;
static EFI_HANDLE KernelHandle;

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

#define EFI_FILE_PROTOCOL_REVISION 0x00010000
#define EFI_FILE_PROTOCOL_REVISION2 0x00020000
#define EFI_FILE_PROTOCOL_LATEST_REVISION EFI_FILE_PROTOCOL_REVISION2

typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

//*******************************************************
// Open Modes
//*******************************************************
#define EFI_FILE_MODE_READ 0x0000000000000001
#define EFI_FILE_MODE_WRITE 0x0000000000000002
#define EFI_FILE_MODE_CREATE 0x8000000000000000
//*******************************************************
// File Attributes
//*******************************************************
#define EFI_FILE_READ_ONLY 0x0000000000000001
#define EFI_FILE_HIDDEN 0x0000000000000002
#define EFI_FILE_SYSTEM 0x0000000000000004
#define EFI_FILE_RESERVED 0x0000000000000008
#define EFI_FILE_DIRECTORY 0x0000000000000010
#define EFI_FILE_ARCHIVE 0x0000000000000020
#define EFI_FILE_VALID_ATTR 0x0000000000000037

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_OPEN) (
	IN EFI_FILE_PROTOCOL *This,
	OUT EFI_FILE_PROTOCOL **NewHandle,
	IN CHAR16 *FileName,
	IN UINT64 OpenMode,
	IN UINT64 Attributes
	);

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_CLOSE) (
	IN EFI_FILE_PROTOCOL *This
	);

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_DELETE) (
	IN EFI_FILE_PROTOCOL *This
	);

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_READ) (
	IN EFI_FILE_PROTOCOL *This,
	IN OUT UINTN *BufferSize,
	OUT VOID *Buffer
	);

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_WRITE) (
	IN EFI_FILE_PROTOCOL *This,
	IN OUT UINTN *BufferSize,
	IN VOID *Buffer
	);

typedef struct {
	EFI_EVENT Event;
	EFI_STATUS Status;
	UINTN BufferSize;
	VOID *Buffer;
} EFI_FILE_IO_TOKEN;

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_OPEN_EX) (
	IN EFI_FILE_PROTOCOL *This,
	OUT EFI_FILE_PROTOCOL **NewHandle,
	IN CHAR16 *FileName,
	IN UINT64 OpenMode,
	IN UINT64 Attributes,
	IN OUT EFI_FILE_IO_TOKEN *Token
	);

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_READ_EX) (
	IN EFI_FILE_PROTOCOL *This,
	IN OUT EFI_FILE_IO_TOKEN *Token
	);

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_WRITE_EX) (
	IN EFI_FILE_PROTOCOL *This,
	IN OUT EFI_FILE_IO_TOKEN *Token
	);

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_FLUSH_EX) (
	IN EFI_FILE_PROTOCOL *This,
	IN OUT EFI_FILE_IO_TOKEN *Token
	);

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_SET_POSITION) (
	IN EFI_FILE_PROTOCOL *This,
	IN UINT64 Position
	);

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_GET_POSITION) (
	IN EFI_FILE_PROTOCOL *This,
	OUT UINT64 *Position
	);

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_GET_INFO) (
	IN EFI_FILE_PROTOCOL *This,
	IN EFI_GUID *InformationType,
	IN OUT UINTN *BufferSize,
	OUT VOID *Buffer
	);

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_SET_INFO) (
	IN EFI_FILE_PROTOCOL *This,
	IN EFI_GUID *InformationType,
	IN UINTN BufferSize,
	IN VOID *Buffer
	);

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_FLUSH) (
	IN EFI_FILE_PROTOCOL *This
	);

#define EFI_FILE_INFO_ID \
{0x9576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b }}

typedef struct {
	UINT64 Size;
	UINT64 FileSize;
	UINT64 PhysicalSize;
	EFI_TIME CreateTime;
	EFI_TIME LastAccessTime;
	EFI_TIME ModificationTime;
	UINT64 Attribute;
	CHAR16 FileName[1];
} EFI_FILE_INFO;

#define EFI_FILE_SYSTEM_INFO_ID \
{0x9576e93, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b }}

typedef struct {
	UINT64 Size;
	BOOLEAN ReadOnly;
	UINT64 VolumeSize;
	UINT64 FreeSpace;
	UINT32 BlockSize;
	CHAR16 VolumeLabel[1];
} EFI_FILE_SYSTEM_INFO;


struct _EFI_FILE_PROTOCOL {
	UINT64 Revision;
	EFI_FILE_OPEN Open;
	EFI_FILE_CLOSE Close;
	EFI_FILE_DELETE Delete;
	EFI_FILE_READ Read;
	EFI_FILE_WRITE Write;
	EFI_FILE_GET_POSITION GetPosition;
	EFI_FILE_SET_POSITION SetPosition;
	EFI_FILE_GET_INFO GetInfo;
	EFI_FILE_SET_INFO SetInfo;
	EFI_FILE_FLUSH Flush;
	EFI_FILE_OPEN_EX OpenEx; // Added for revision 2
	EFI_FILE_READ_EX ReadEx; // Added for revision 2
	EFI_FILE_WRITE_EX WriteEx; // Added for revision 2
	EFI_FILE_FLUSH_EX FlushEx; // Added for revision 2
} ;

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID {0x0964e5b22,0x6459,0x11d2,\
{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}}

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION 0x00010000

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME) (
	IN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
	OUT EFI_FILE_PROTOCOL **Root
	);

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
	UINT64 Revision;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
};

static char16_t* unknownError = u"Unknown Error Code";

#define EFI_ERRORS_SIZE 16

static char16_t* efiErrors[EFI_ERRORS_SIZE] = {
	u"Success",
	u"Load Error",
	u"Invalid Parameter",
	u"Unsupported",
	u"Bad Buffer Size",
	u"Buffer Too Small",
	u"Not Ready",
	u"Device Error",
	u"Write Protected",
	u"Out of Resources",
	u"Volume Corrupted",
	u"Volume Full",
	u"No Media",
	u"Media Changed",
	u"Not Found",
	u"Access Denied"
};

#define EFI_WARNINGS_SIZE 1
static char16_t* efiWarnings[EFI_WARNINGS_SIZE] = {
	u"Success",
};

char16_t* getError(const EFI_STATUS error) {
	if (EFI_ERROR(error))
	{
		UINT32 code = (UINT32)(error & ~MAX_BIT);
		if (code >= EFI_ERRORS_SIZE)
			return unknownError;
		else
			return efiErrors[code];
	}
	else
	{
		if (error >= EFI_WARNINGS_SIZE)
			return unknownError;
		else
			return efiWarnings[error];
	}
}

static EFI_LOADED_IMAGE_PROTOCOL* Kernel = nullptr;

static EFI_STATUS errno = EFI_SUCCESS;

static EFI_FILE_PROTOCOL* pwd = nullptr;

EFI_STATUS GetBootFileSystem(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL** Protocol)
{
	EFI_GUID fsprotid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
	return BS->HandleProtocol(Kernel->DeviceHandle, &fsprotid, (void**)Protocol);
}

EFI_FILE* OpenFile(const char16_t* filename, const char* mode)
{
	EFI_FILE* ret = nullptr;
	
	EFI_FILE_PROTOCOL* file;
	UINT64 openmode = EFI_FILE_MODE_READ;
	while (char c = *mode++)
	{
		if (c == 'w')
			openmode |= EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE;
		else if (c == 'a')
			openmode |= EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE;
	}

	if (errno = pwd->Open(pwd, &file, (CHAR16*)filename, openmode, 0))
		goto rootexit;
	ret = (EFI_FILE*)file;
rootexit:
	return ret;
}

EFI_STATUS CloseFile(EFI_FILE* file)
{
	EFI_FILE_PROTOCOL* filep = (EFI_FILE_PROTOCOL*)file;
	return filep->Close(filep);
}

UINT64 GetFileSize(EFI_FILE* file)
{
	EFI_FILE_PROTOCOL* filep = (EFI_FILE_PROTOCOL*)file;
	EFI_GUID fileInfo = EFI_FILE_INFO_ID;
	UINT64 fileSize = 0;
	UINTN bufSize = 0;
	filep->GetInfo(filep, &fileInfo, &bufSize, NULL);
	EFI_FILE_INFO* buffer = (EFI_FILE_INFO*)kmalloc(bufSize);
	if (EFI_ERROR(filep->GetInfo(filep, &fileInfo, &bufSize, buffer)))
		goto end;
	fileSize = buffer->FileSize;
end:
	kfree(buffer);
	return fileSize;
}

UINTN ReadFile(void* buffer, UINTN size, UINTN count, EFI_FILE* file)
{
	EFI_FILE_PROTOCOL* filep = (EFI_FILE_PROTOCOL*)file;
	UINTN length = size*count;
	errno = filep->Read(filep, &length, buffer);
	return length / size;
}

UINTN WriteFile(void* buffer, UINTN size, UINTN count, EFI_FILE* file)
{
	EFI_FILE_PROTOCOL* filep = (EFI_FILE_PROTOCOL*)file;
	UINTN length = size * count;
	errno = filep->Write(filep, &length, buffer);
	return length / size;
}

EFI_FILE_PROTOCOL* GetRootDirectory()
{
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* bootfs;
	if (errno = GetBootFileSystem(&bootfs))
		return nullptr;
	EFI_FILE_PROTOCOL* root;
	if (errno = bootfs->OpenVolume(bootfs, &root))
		return nullptr;
	return root;
}

EFI_STATUS SetWorkingDirectory(const char16_t* directory)
{
	if (!directory)
	{
		if (pwd)
			pwd->Close(pwd);
		pwd = nullptr;
		return errno;
	}
	else
	{
		if (!pwd)
			if (!(pwd = GetRootDirectory()))
				return errno;
		EFI_FILE_PROTOCOL* result;
		EFI_STATUS stat = pwd->Open(pwd, &result, (CHAR16*)directory, EFI_FILE_MODE_READ, 0);
		if (EFI_ERROR(stat))
			return stat;
		stat = pwd->Close(pwd);
		pwd = result;
		return stat;
	}
}

EFI_STATUS InitializeLib(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	ST = SystemTable;
	BS = SystemTable->BootServices;
	RS = SystemTable->RuntimeServices;
	KernelHandle = ImageHandle;
	setKtermOutputProc(&uefi_puts);
	setLiballocAllocator(&uefi_alloc, &uefi_free);
	//Get boot information
	EFI_STATUS Status;
	EFI_GUID loadedimageprot = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_LOADED_IMAGE_PROTOCOL* loadedimage = nullptr;
	if (Status = BS->HandleProtocol(ImageHandle, &loadedimageprot, (void**)&loadedimage))
	{
		printf(u"Could not get boot information: %s\r\n", getError(Status));
		return Status;
	}
	Kernel = loadedimage;
	return EFI_SUCCESS;
}

size_t GetIntegerInput(char16_t* prompt)
{
	const size_t KEYSIZE = 16;
	EFI_INPUT_KEY keys[KEYSIZE];
	size_t keyIndex = 0;
	size_t value = 0;
	UINTN index = 0;
	EFI_STATUS Status;
	puts(prompt);
	ST->ConOut->EnableCursor(ST->ConOut, TRUE);
	for (; keyIndex < KEYSIZE;)
	{
		BS->WaitForEvent(1, &ST->ConIn->WaitForKey, &index);
		Status = ST->ConIn->ReadKeyStroke(ST->ConIn, &keys[keyIndex]);
		if (keys[keyIndex].UnicodeChar == CHAR_LINEFEED || keys[keyIndex].UnicodeChar == CHAR_CARRIAGE_RETURN)
		{
			puts(u"\r\n");
			break;
		}
		else if (keys[keyIndex].UnicodeChar >= '0' && keys[keyIndex].UnicodeChar <= '9')
		{
			char16_t buf[2];
			buf[0] = keys[keyIndex].UnicodeChar;
			buf[1] = u'\0';
			puts(buf);
			value *= 10;
			value += keys[keyIndex].UnicodeChar - '0';
			++keyIndex;
		}
		else if (keys[keyIndex].UnicodeChar == CHAR_BACKSPACE)
		{
			puts(u"\b \b");
			--keyIndex;
		}
		else
		{
			puts(u"\u2588\b");
		}
	}
	ST->ConOut->EnableCursor(ST->ConOut, FALSE);
	return value;
}

UINT32 IterateGraphicsMode(gmode_callback callback)
{
	EFI_GRAPHICS_OUTPUT_PROTOCOL* efigop = nullptr;
	EFI_GUID gopguid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	if (EFI_ERROR(BS->LocateProtocol(&gopguid, NULL, (void**)&efigop)))
	{
		return UINT32_MAX;
	}
	else
	{
		auto mode = efigop->Mode;
		for (size_t i = 0; i < mode->MaxMode; ++i)
		{
			UINTN sizeOfInfo;
			EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* minfo;
			efigop->QueryMode(efigop, i, &sizeOfInfo, &minfo);
			if (callback(i, minfo))
				return i;
		}
		return UINT32_MAX;
	}
}

EFI_STATUS GetGraphicsModeInfo(UINT32 Mode, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION** info, UINTN* SizeOfInfo)
{
	EFI_GRAPHICS_OUTPUT_PROTOCOL* efigop = nullptr;
	EFI_GUID gopguid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_STATUS Status;
	if (EFI_ERROR(Status = BS->LocateProtocol(&gopguid, NULL, (void**)&efigop)))
	{
		return Status;
	}
	else
	{
		if (Mode == UINT32_MAX)
			Mode = efigop->Mode->Mode;
		efigop->QueryMode(efigop, Mode, SizeOfInfo, info);
	}
	return Status;
}

static FRAMEBUFFER_INFORMATION information;

EFI_STATUS SetGraphicsMode(UINT32 Mode)
{
	EFI_GRAPHICS_OUTPUT_PROTOCOL* efigop = nullptr;
	EFI_GUID gopguid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_STATUS Status;
	if (EFI_ERROR(Status = BS->LocateProtocol(&gopguid, NULL, (void**)&efigop)))
	{
		return Status;
	}
	else
	{
		ST->ConOut->SetAttribute(ST->ConOut, EFI_BACKGROUND_BLUE | EFI_WHITE);
		ST->ConOut->SetCursorPosition(ST->ConOut, 0, 0);
		Status = efigop->SetMode(efigop, Mode);
		auto info = efigop->Mode->Info;
		//Clear screen
		EFI_GRAPHICS_OUTPUT_BLT_PIXEL pixel;
		pixel.Blue = 0xAA;
		pixel.Green = 0;
		pixel.Red = 0;

		efigop->Blt(efigop, &pixel, EfiBltVideoFill, 0, 0, 0, 0, info->HorizontalResolution, info->VerticalResolution, 0);
		//Now we need to emulate the console ourselves, as some UEFI implementations change mode back
		information.phyaddr = (void*)efigop->Mode->FrameBufferBase;
		information.size = efigop->Mode->FrameBufferSize;
		information.pixelsPerLine = efigop->Mode->Info->PixelsPerScanLine;
		information.Horizontal = efigop->Mode->Info->HorizontalResolution;
		information.Vertical = efigop->Mode->Info->VerticalResolution;

		switch (efigop->Mode->Info->PixelFormat)
		{
		case PixelRedGreenBlueReserved8BitPerColor:
			information.redmask = 0xFF;
			information.greenmask = 0xFF00;
			information.bluemask = 0xFF0000;
			information.resvmask = 0xFF000000;
			break;
		case PixelBlueGreenRedReserved8BitPerColor:
			information.redmask = 0xFF0000;
			information.greenmask = 0xFF00;
			information.bluemask = 0xFF;
			information.resvmask = 0xFF000000;
			break;
		case PixelBitMask:
			information.redmask = efigop->Mode->Info->PixelInformation.RedMask;
			information.greenmask = efigop->Mode->Info->PixelInformation.GreenMask;
			information.bluemask = efigop->Mode->Info->PixelInformation.BlueMask;
			information.resvmask = efigop->Mode->Info->PixelInformation.ReservedMask;
			break;
		}
		InitialiseGraphics(information);
		setKtermOutputProc(&gputs_k);
	}
	return Status;
}

EFI_STATUS PrepareExitBootServices()
{
	//SetWorkingDirectory(nullptr);	//Free the working directory handle
	//Startup internal graphics
	setKtermOutputProc(&gputs_k);
	return EFI_SUCCESS;
}

EFI_STATUS getErrno()
{
	return errno;
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

void get_framebuffer_info(PFRAMEBUFFER_INFORMATION& fbinfo)
{
	fbinfo = &information;
}

int strcmp_basic(const char* s1, const char* s2)
{
	while (*s1 && *s2)
	{
		if (*s1 != *s2)
			return *s1 - *s2;
		++s1; ++s2;
	}
	return *s1 - *s2;
}

char toupper(char c)
{
	if (c >= 'a' && c <= 'z')
	{
		return c - 'a' + 'A';
	}
	return c;
}

int strcmp_nocase(const char* s1, const char* s2)
{
	while (*s1 && *s2)
	{
		if (toupper(*s1) != toupper(*s2))
		{
			return *s1 - *s2;

		}
		++s1; ++s2;
	}
	return *s1 - *s2;
}