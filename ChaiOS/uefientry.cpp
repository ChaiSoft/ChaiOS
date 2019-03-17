#include <Uefi.h>
#include "acpi.h"
#include "kterm.h"

static EFI_SYSTEM_TABLE* ST = nullptr;

static void uefi_puts(const char16_t* p)
{
	ST->ConOut->OutputString(ST->ConOut, (CHAR16*)p);;
}


EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	ST = SystemTable;
	setKtermOutputProc(&uefi_puts);
	puts(u"ChaiOS 0.09\n");
	EFI_STATUS Status;
	EFI_INPUT_KEY Key;
	//Read ACPI configuration tables
	startup_acpi(SystemTable);
	
	while ((Status = SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key)) == EFI_NOT_READY);
	return EFI_SUCCESS;
}