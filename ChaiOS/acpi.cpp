#include "acpi.h"
#include "kterm.h"

#define EFI_ACPI_20_TABLE_GUID \
{0x8868e871,0xe4f1,0x11d3,\
{0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}}

#define ACPI_TABLE_GUID \
{0xeb9d2d30,0x2d88,0x11d3,\
{0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}}

static bool compare_array(const UINT8* a1, const UINT8* a2, size_t n = 8)
{
	for (size_t i = 0; i < n; i++)
		if (a1[i] != a2[i]) return false;
	return true;
}
static bool compare_guid(const EFI_GUID& g1, const EFI_GUID& g2)
{
	return ((g1.Data1 == g2.Data1) && (g1.Data2 == g2.Data2) && (g1.Data3 == g2.Data3) && compare_array(g1.Data4, g2.Data4));
}

static void* RSDP = 0;

static void acpi10start(EFI_ACPI_1_0_ROOT_SYSTEM_DESCRIPTION_POINTER* rsdp)
{
	EFI_ACPI_DESCRIPTION_HEADER* rsdt = (EFI_ACPI_DESCRIPTION_HEADER*)rsdp->RsdtAddress;
	UINT32* tablebases = (UINT32*)&rsdt[1];
}

static void acpi20start(EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER* rsdp)
{
	EFI_ACPI_DESCRIPTION_HEADER* xsdt = (EFI_ACPI_DESCRIPTION_HEADER*)rsdp->XsdtAddress;
	UINT64* tablebases = (UINT64*)&xsdt[1];
	
}

void startup_acpi(EFI_SYSTEM_TABLE *SystemTable)
{
	int bestacpi = 0;
	VOID* bestacpitab = nullptr;
	for (unsigned int n = 0; n < SystemTable->NumberOfTableEntries; n++)
	{
		static const EFI_GUID acpi20 = EFI_ACPI_20_TABLE_GUID;
		static const EFI_GUID acpi10 = ACPI_TABLE_GUID;
		if (compare_guid(SystemTable->ConfigurationTable[n].VendorGuid, acpi20))
		{
			bestacpi = 2;
			bestacpitab = SystemTable->ConfigurationTable[n].VendorTable;
			break;
		}
		else if (compare_guid(SystemTable->ConfigurationTable[n].VendorGuid, acpi10))
		{
			bestacpi = 1;
			bestacpitab = SystemTable->ConfigurationTable[n].VendorTable;
		}
	}
	if (bestacpi == 0)
	{
		puts(u"ACPI unsupported\n");
		return;
	}
	else
		puts(u"Found ACPI tables\n");

	RSDP = bestacpitab;
	if (bestacpi == 1)
		acpi10start((EFI_ACPI_1_0_ROOT_SYSTEM_DESCRIPTION_POINTER*)RSDP);
	else
		acpi20start((EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER*)RSDP);
}