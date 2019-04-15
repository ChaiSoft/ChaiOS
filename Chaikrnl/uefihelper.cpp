#include <stdheaders.h>
#include "uefihelper.h"
#include "acpihelp.h"

typedef struct {
	uint64_t Signature;
	uint32_t Revision;
	uint32_t HeaderSize;
	uint32_t CRC32;
	uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef void* EFI_HANDLE;
typedef void EFI_UNKNOWN_PROTOCOL;

__declspec(align(8)) typedef struct {
	uint32_t Data1;
	uint16_t Data2;
	uint16_t Data3;
	uint8_t Data4[8];
}EFI_GUID;

typedef struct {
	EFI_GUID VendorGuid;
	void *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef void* EFI_UNKNOWN_FUNCTION;

typedef struct {
	uint16_t  Year;
	uint8_t   Month;
	uint8_t   Day;
	uint8_t   Hour;
	uint8_t   Minute;
	uint8_t   Second;
	uint8_t   Pad1;
	uint32_t  Nanosecond;
	int16_t   TimeZone;
	uint8_t   Daylight;
	uint8_t   Pad2;
} EFI_TIME;

typedef struct {
	uint32_t    Resolution;
	uint32_t    Accuracy;
	uint8_t   SetsToZero;
} EFI_TIME_CAPABILITIES;

typedef
size_t
(__cdecl *EFI_GET_TIME)(
	EFI_TIME                    *Time,
	EFI_TIME_CAPABILITIES       *Capabilities
	);

typedef struct {
	EFI_TABLE_HEADER Hdr;
	EFI_GET_TIME GetTime;
	EFI_UNKNOWN_FUNCTION SetTime;
	EFI_UNKNOWN_FUNCTION GetWakeupTime;
	EFI_UNKNOWN_FUNCTION SetWakeupTime;
	EFI_UNKNOWN_FUNCTION SetVirtualAddressMap;
	EFI_UNKNOWN_FUNCTION ConvertPointer;
	EFI_UNKNOWN_FUNCTION GetVariable;
	EFI_UNKNOWN_FUNCTION GetNextVariableName;
	EFI_UNKNOWN_FUNCTION SetVariable;
	EFI_UNKNOWN_FUNCTION GetNextHighMonotonicCount;
	EFI_UNKNOWN_FUNCTION ResetSystem;
	EFI_UNKNOWN_FUNCTION UpdateCapsule;
	EFI_UNKNOWN_FUNCTION QueryCapsuleCapabilities;
	EFI_UNKNOWN_FUNCTION QueryVariableInfo;
} EFI_RUNTIME_SERVICES;

typedef struct {
	EFI_TABLE_HEADER Hdr;
	char16_t *FirmwareVendor;
	uint32_t FirmwareRevision;
	EFI_HANDLE ConsoleInHandle;
	EFI_UNKNOWN_PROTOCOL *ConIn;
	EFI_HANDLE ConsoleOutHandle;
	EFI_UNKNOWN_PROTOCOL *ConOut;
	EFI_HANDLE StandardErrorHandle;
	EFI_UNKNOWN_PROTOCOL *StdErr;
	EFI_RUNTIME_SERVICES *RuntimeServices;
	EFI_UNKNOWN_PROTOCOL *BootServices;
	size_t NumberOfTableEntries;
	EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

static EFI_SYSTEM_TABLE* ST;

static bool compare_array(const uint8_t* a1, const uint8_t* a2, size_t n = 8)
{
	for (size_t i = 0; i < n; i++)
		if (a1[i] != a2[i]) return false;
	return true;
}

static bool compare_guids(const EFI_GUID& g1, const EFI_GUID& g2)
{
	return ((g1.Data1 == g2.Data1) && (g1.Data2 == g2.Data2) && (g1.Data3 == g2.Data3) && compare_array(g1.Data4, g2.Data4));
}

static const EFI_GUID acpi_20 = { 0x8868e871,0xe4f1,0x11d3,\
{0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81} };

static const EFI_GUID acpi_10 = { 0xeb9d2d30,0x2d88,0x11d3,\
{0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d} };

static uint64_t AcpiSystemTimer() {
	EFI_TIME time;
	ST->RuntimeServices->GetTime(&time, nullptr);
	uint64_t timer;
	uint16_t year = time.Year;
	uint8_t month = time.Month;
	uint8_t day = time.Day;
	timer = ((uint64_t)(year / 4 - year / 100 + year / 400 + 367 * month / 12 + day) +
		year * 365 - 719499);
	timer *= 24;
	timer += time.Hour;
	timer *= 60;
	timer += time.Minute;
	timer *= 60;
	timer += time.Second;
	timer *= 10000000;	//100 n intervals
	timer += time.Nanosecond / 100;
	return timer;
}

void pull_system_table_data(void* systab)
{
	ST = reinterpret_cast<EFI_SYSTEM_TABLE*>(systab);
	void* bestacpi = nullptr;
	for (size_t n = 0; n < ST->NumberOfTableEntries; ++n)
	{
		if (compare_guids(ST->ConfigurationTable[n].VendorGuid, acpi_10) && !bestacpi)
		{
			bestacpi = ST->ConfigurationTable[n].VendorTable;
		}
		else if (compare_guids(ST->ConfigurationTable[n].VendorGuid, acpi_20))
		{
			bestacpi = ST->ConfigurationTable[n].VendorTable;
		}
	}
	set_rsdp(bestacpi);
	set_acpi_timer(&AcpiSystemTimer);
}