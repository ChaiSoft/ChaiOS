#include <Uefi.h>
#include "acpihelp.h"
#include <acpi.h>
#include "kterm.h"
#include "uefilib.h"
#include "liballoc.h"
#include "assembly.h"
#include "multiprocessor.h"


extern bool CallConstructors();

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	InitializeLib(ImageHandle, SystemTable);
	CallConstructors();
	puts(u"ChaiOS 0.09\r\n");
	EFI_STATUS Status;
	EFI_INPUT_KEY Key;
	//Read ACPI configuration tables
	startup_acpi(SystemTable);
	startup_multiprocessor();
	char buffer[49];
	buffer[48] = '\0';
	cpuid(0x80000002, (size_t*)&buffer[0], (size_t*)&buffer[4], (size_t*)&buffer[8], (size_t*)&buffer[12]);
	cpuid(0x80000003, (size_t*)&buffer[16], (size_t*)&buffer[20], (size_t*)&buffer[24], (size_t*)&buffer[28]);
	cpuid(0x80000004, (size_t*)&buffer[32], (size_t*)&buffer[36], (size_t*)&buffer[40], (size_t*)&buffer[44]);
	puts(buffer);
	puts("\r\n");

	while ((Status = SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key)) == EFI_NOT_READY);
	return EFI_SUCCESS;
}