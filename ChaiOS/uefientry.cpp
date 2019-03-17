#include <Uefi.h>
#include "acpi.h"
#include <acpi.h>
#include "kterm.h"
#include "uefilib.h"
#include "liballoc.h"
#include "assembly.h"


EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	InitializeLib(ImageHandle, SystemTable);
	puts(u"ChaiOS 0.09\r\n");
	EFI_STATUS Status;
	EFI_INPUT_KEY Key;
	//Read ACPI configuration tables
	startup_acpi(SystemTable);
	ACPI_TABLE_MADT* madt = nullptr;
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt)))
		puts(u"Successfully found MADT\r\n");
	printf(u"LAPIC address: %x\r\n", (size_t)madt->Address);
	//Count the CPUs
	ACPI_SUBTABLE_HEADER* madtsubs = (ACPI_SUBTABLE_HEADER*)&madt[1];
	size_t numcpus = 0; size_t enabledcpus = 0;
	while ((char*)madtsubs - (char*)madt < madt->Header.Length)
	{
		if (madtsubs->Type == ACPI_MADT_TYPE_LOCAL_APIC)
		{
			ACPI_MADT_LOCAL_APIC* lapic = (ACPI_MADT_LOCAL_APIC*)madtsubs;
			++numcpus;
			if (lapic->LapicFlags & ACPI_MADT_ENABLED)
			{
				printf(u"Active CPU with ID %d (ACPI: %d)\r\n", lapic->Id, lapic->ProcessorId);
				++enabledcpus;
			}
			else
			{
				printf(u"Disabled CPU with ID %d (ACPI: %d)\r\n", lapic->Id, lapic->ProcessorId);
			}
		}
		madtsubs = (ACPI_SUBTABLE_HEADER*)((char*)madtsubs + madtsubs->Length);
	}
	printf(u"%d CPUs, %d enabled\r\n", numcpus, enabledcpus);
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