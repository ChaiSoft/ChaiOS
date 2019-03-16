#include <Uefi.h>
#include "acpi.h"

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	CHAR16* chaiosver = (CHAR16*)L"ChaiOS 0.09\n";
	EFI_STATUS Status;
	EFI_INPUT_KEY Key;
	SystemTable->ConOut->OutputString(SystemTable->ConOut, chaiosver);
	//Read ACPI configuration tables
	startup_acpi(SystemTable);
	
	while ((Status = SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key)) == EFI_NOT_READY);
	return EFI_SUCCESS;
}