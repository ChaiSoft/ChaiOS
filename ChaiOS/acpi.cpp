#include "acpi.h"
#include "kterm.h"
#include "liballoc.h"
#include "uefilib.h"
#include <acpi.h>
#include "assembly.h"

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
		puts(u"ACPI unsupported\r\n");
		return;
	}
	else
		puts(u"Found ACPI tables\r\n");

	RSDP = bestacpitab;
	AcpiInitializeTables(0, 0, FALSE);
}

extern "C" {

ACPI_STATUS AcpiOsInitialize()
{
}

ACPI_STATUS AcpiOsTerminate()
{
}

ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer()
{
	if (!RSDP)
	{
		AcpiFindRootPointer((ACPI_PHYSICAL_ADDRESS*)&RSDP);
	}
	return (ACPI_PHYSICAL_ADDRESS)RSDP;
}

ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *PredefinedObject, ACPI_STRING *NewValue)
{
	*NewValue = 0;
	return AE_OK;
}
ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER *ExistingTable, ACPI_TABLE_HEADER **NewTable)
{
	*NewTable = 0;
	return AE_OK;
}
ACPI_STATUS AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER *ExistingTable, ACPI_PHYSICAL_ADDRESS *NewAddress, UINT32 *NewTableLength)
{
	*NewAddress = 0;
	*NewTableLength = 0;
	return AE_OK;
}
void *AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS PhysicalAddress, ACPI_SIZE Length)
{
	return (void*)PhysicalAddress;
}
void AcpiOsUnmapMemory(void *where, ACPI_SIZE length)
{

}
ACPI_STATUS AcpiOsGetPhysicalAddress(void *LogicalAddress, ACPI_PHYSICAL_ADDRESS *PhysicalAddress)
{
	*PhysicalAddress = (ACPI_PHYSICAL_ADDRESS)LogicalAddress;
}
void *AcpiOsAllocate(ACPI_SIZE Size)
{
	return kmalloc((size_t)Size);
}
void AcpiOsFree(void *Memory)
{
	return kfree(Memory);
}
BOOLEAN AcpiOsReadable(void *Memory, ACPI_SIZE Length)
{
	return TRUE;
}
BOOLEAN AcpiOsWritable(void *Memory, ACPI_SIZE Length)
{
	return TRUE;
}
ACPI_THREAD_ID AcpiOsGetThreadId()
{
	return 0;
}
ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void *Context)
{
	Function(Context);
	return AE_OK;
}
void AcpiOsSleep(UINT64 Milliseconds)
{
	Stall(Milliseconds * 1000);
}
void AcpiOsStall(UINT32 Microseconds)
{
	Stall(Microseconds);
}

ACPI_STATUS AcpiOsCreateMutex(ACPI_MUTEX *OutHandle)
{
	return AE_OK;
}
void AcpiOsDeleteMutex(ACPI_MUTEX Handle)
{

}
ACPI_STATUS AcpiOsAcquireMutex(ACPI_MUTEX Handle, UINT16 Timeout)
{
	return AE_OK;
}
void AcpiOsReleaseMutex(ACPI_MUTEX Handle)
{

}

ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE *OutHandle)
{
	return AE_OK;
}
ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle)
{
	return AE_OK;
}
ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout)
{
	return AE_OK;
}
ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units)
{
	return AE_OK;
}
ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle)
{
	return AE_OK;
}
void AcpiOsDeleteLock(ACPI_HANDLE Handle)
{

}
ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK Handle)
{
	return 0;
}
void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags)
{
}
ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler, void *Context)
{
	return AE_OK;
}
ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER Handler)
{
	return AE_OK;
}

void ACPI_INTERNAL_VAR_XFACE AcpiOsPrintf(const char *Format, ...)
{
	
}

void AcpiOsVprintf(const char *Format, va_list Args)
{

}

int AcpiOsAcquireGlobalLock(UINT32 *lock)
{
	return AE_OK;
}
int AcpiOsReleaseGlobalLock(UINT32 *lock)
{
	return AE_OK;
}

ACPI_STATUS AcpiOsSignal(UINT32 Function, void *Info)
{
	return AE_NOT_FOUND;
}

#define EFI_PCI_IO_PROTOCOL_GUID {0x4cf5b200,0x68b8,0x4ca5,\
{0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}}

typedef enum {
	EfiPciIoWidthUint8,
	EfiPciIoWidthUint16,
	EfiPciIoWidthUint32,
	EfiPciIoWidthUint64,
	EfiPciIoWidthFifoUint8,
	EfiPciIoWidthFifoUint16,
	EfiPciIoWidthFifoUint32,
	EfiPciIoWidthFifoUint64,
	EfiPciIoWidthFillUint8,
	EfiPciIoWidthFillUint16,
	EfiPciIoWidthFillUint32,
	EfiPciIoWidthFillUint64,
	EfiPciIoWidthMaximum
} EFI_PCI_IO_PROTOCOL_WIDTH;

typedef enum {
	EfiPciIoAttributeOperationGet,
	EfiPciIoAttributeOperationSet,
	EfiPciIoAttributeOperationEnable,
	EfiPciIoAttributeOperationDisable,
	EfiPciIoAttributeOperationSupported,
	EfiPciIoAttributeOperationMaximum
} EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION;

typedef enum {
	EfiPciIoOperationBusMasterRead,
	EfiPciIoOperationBusMasterWrite,
	EfiPciIoOperationBusMasterCommonBuffer, EfiPciIoOperationMaximum
} EFI_PCI_IO_PROTOCOL_OPERATION;

//Advance declaration
struct _EFI_PCI_IO_PROTOCOL;
typedef struct _EFI_PCI_IO_PROTOCOL EFI_PCI_IO_PROTOCOL;

//*******************************************************
// EFI_PCI_IO_PROTOCOL_POLL_IO_MEM
//*******************************************************
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_POLL_IO_MEM) (IN EFI_PCI_IO_PROTOCOL *This,
	IN EFI_PCI_IO_PROTOCOL_WIDTH Width,
	IN UINT8 BarIndex,
	IN UINT64 Offset,
	IN UINT64 Mask,
	IN UINT64 Value,
	IN UINT64 Delay,
	OUT UINT64 *Result);
//*******************************************************
// EFI_PCI_IO_PROTOCOL_IO_MEM
//*******************************************************
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_IO_MEM) (IN EFI_PCI_IO_PROTOCOL *This,
	IN EFI_PCI_IO_PROTOCOL_WIDTH Width,
	IN UINT8 BarIndex,
	IN UINT64 Offset,
	IN UINTN Count,
	IN OUT VOID *Buffer);

//*******************************************************
// EFI_PCI_IO_PROTOCOL_ACCESS
//*******************************************************
typedef struct {
	EFI_PCI_IO_PROTOCOL_IO_MEM Read;
	EFI_PCI_IO_PROTOCOL_IO_MEM Write;
} EFI_PCI_IO_PROTOCOL_ACCESS;
//*******************************************************
// EFI_PCI_IO_PROTOCOL_CONFIG
//*******************************************************
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_CONFIG) (IN EFI_PCI_IO_PROTOCOL *This,
	IN EFI_PCI_IO_PROTOCOL_WIDTH Width,
	IN UINT32 Offset,
	IN UINTN Count,
	IN OUT VOID *Buffer
	);
//*******************************************************
// EFI_PCI_IO_PROTOCOL_CONFIG_ACCESS
//*******************************************************
typedef struct {
	EFI_PCI_IO_PROTOCOL_CONFIG Read; EFI_PCI_IO_PROTOCOL_CONFIG Write;
} EFI_PCI_IO_PROTOCOL_CONFIG_ACCESS;

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_COPY_MEM) (
	IN EFI_PCI_IO_PROTOCOL *This,
	IN EFI_PCI_IO_PROTOCOL_WIDTH Width,
	IN UINT8 DestBarIndex,
	IN UINT64 DestOffset,
	IN UINT8 SrcBarIndex,
	IN UINT64 SrcOffset,
	IN UINTN Count
	);

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_MAP) (
	IN EFI_PCI_IO_PROTOCOL *This,
	IN EFI_PCI_IO_PROTOCOL_OPERATION Operation,
	IN VOID *HostAddress,
	IN OUT UINTN *NumberOfBytes,
	OUT EFI_PHYSICAL_ADDRESS *DeviceAddress,
	OUT VOID **Mapping
	);

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_UNMAP) (
	IN EFI_PCI_IO_PROTOCOL *This,
	IN VOID *Mapping
	);

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_ALLOCATE_BUFFER) (
	IN EFI_PCI_IO_PROTOCOL *This,
	IN EFI_ALLOCATE_TYPE Type,
	IN EFI_MEMORY_TYPE MemoryType,
	IN UINTN Pages,
	OUT VOID **HostAddress,
	IN UINT64 Attributes
	);

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_FREE_BUFFER) (
	IN EFI_PCI_IO_PROTOCOL *This,
	IN UINTN Pages,
	IN VOID *HostAddress
	);

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_FLUSH) (
	IN EFI_PCI_IO_PROTOCOL *This
	);

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_GET_LOCATION) (
	IN EFI_PCI_IO_PROTOCOL *This,
	OUT UINTN *SegmentNumber,
	OUT UINTN *BusNumber,
	OUT UINTN *DeviceNumber,
	OUT UINTN *FunctionNumber
	);

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_ATTRIBUTES) (
	IN EFI_PCI_IO_PROTOCOL *This,
	IN EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION Operation,
	IN UINT64 Attributes,
	OUT UINT64 *Result OPTIONAL
	);

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_GET_BAR_ATTRIBUTES) (
	IN EFI_PCI_IO_PROTOCOL *This,
	IN UINT8 BarIndex,
	OUT UINT64 *Supports OPTIONAL,
	OUT VOID **Resources OPTIONAL
	);

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_SET_BAR_ATTRIBUTES) (
	IN EFI_PCI_IO_PROTOCOL *This,
	IN UINT64 Attributes,
	IN UINT8 BarIndex,
	IN OUT UINT64 *Offset,
	IN OUT UINT64 *Length
	);

typedef struct _EFI_PCI_IO_PROTOCOL {
	EFI_PCI_IO_PROTOCOL_POLL_IO_MEM PollMem;
	EFI_PCI_IO_PROTOCOL_POLL_IO_MEM PollIo;
	EFI_PCI_IO_PROTOCOL_ACCESS Mem;
	EFI_PCI_IO_PROTOCOL_ACCESS Io;
	EFI_PCI_IO_PROTOCOL_CONFIG_ACCESS Pci;
	EFI_PCI_IO_PROTOCOL_COPY_MEM CopyMem;
	EFI_PCI_IO_PROTOCOL_MAP Map;
	EFI_PCI_IO_PROTOCOL_UNMAP Unmap; EFI_PCI_IO_PROTOCOL_ALLOCATE_BUFFER AllocateBuffer;
	EFI_PCI_IO_PROTOCOL_FREE_BUFFER FreeBuffer; EFI_PCI_IO_PROTOCOL_FLUSH Flush; EFI_PCI_IO_PROTOCOL_GET_LOCATION GetLocation;
	EFI_PCI_IO_PROTOCOL_ATTRIBUTES Attributes;
	EFI_PCI_IO_PROTOCOL_GET_BAR_ATTRIBUTES GetBarAttributes;
	EFI_PCI_IO_PROTOCOL_SET_BAR_ATTRIBUTES SetBarAttributes;
	UINT64 RomSize;
	VOID *RomImage;
} EFI_PCI_IO_PROTOCOL;

/******************************************************************************
*
* FUNCTION:    AcpiEfiGetPciDev
*
* PARAMETERS:  PciId               - Seg/Bus/Dev
*
* RETURN:      ACPI_EFI_PCI_IO that matches PciId. Null on error.
*
* DESCRIPTION: Find ACPI_EFI_PCI_IO for the given PciId.
*
*****************************************************************************/

static EFI_PCI_IO_PROTOCOL *
AcpiEfiGetPciDev(
	ACPI_PCI_ID             *PciId)
{
	EFI_STATUS         EfiStatus;
	UINTN                   i;
	UINTN                   HandlesCount = 0;
	EFI_HANDLE         *Handles = 0;
	EFI_GUID           EfiPciIoProtocol = EFI_PCI_IO_PROTOCOL_GUID;
	EFI_PCI_IO_PROTOCOL         *PciIoProtocol = 0;
	UINTN                   Bus;
	UINTN                   Device;
	UINTN                   Function;
	UINTN                   Segment;

	EFI_BOOT_SERVICES* BS = GetBootServices();
	EfiStatus = BS->LocateHandleBuffer(EFI_LOCATE_SEARCH_TYPE::ByProtocol,
		&EfiPciIoProtocol, NULL, &HandlesCount, &Handles);
	if (EFI_ERROR(EfiStatus))
	{
		return nullptr;
	}

	for (i = 0; i < HandlesCount; i++)
	{
		EfiStatus = BS->HandleProtocol(Handles[i],
			&EfiPciIoProtocol, (VOID **)&PciIoProtocol);
		if (!EFI_ERROR(EfiStatus))
		{
			EfiStatus = PciIoProtocol->GetLocation(PciIoProtocol, &Segment, &Bus, &Device, &Function);
			if (!EFI_ERROR(EfiStatus) &&
				Bus == PciId->Bus &&
				Device == PciId->Device &&
				Function == PciId->Function &&
				Segment == PciId->Segment)
			{
				BS->FreePool(Handles);
				return (PciIoProtocol);
			}
		}
	}

	BS->FreePool(Handles);
	return nullptr;
}

ACPI_STATUS AcpiOsReadPciConfiguration(
	ACPI_PCI_ID             *PciId,
	UINT32                  PciRegister,
	UINT64                  *Value64,
	UINT32                  Width)
{
	EFI_PCI_IO_PROTOCOL         *PciIo;
	EFI_STATUS         EfiStatus;
	UINT8                   Value8;
	UINT16                  Value16;
	UINT32                  Value32;


	*Value64 = 0;
	PciIo = AcpiEfiGetPciDev(PciId);
	if (PciIo == NULL)
	{
		return (AE_NOT_FOUND);
	}

	switch (Width)
	{
	case 8:
		EfiStatus = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint8, PciRegister, 1, (VOID *)&Value8);
		*Value64 = Value8;
		break;

	case 16:
		EfiStatus = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint16, PciRegister, 1, (VOID *)&Value16);
		*Value64 = Value16;
		break;

	case 32:
		EfiStatus = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint32, PciRegister, 1, (VOID *)&Value32);
		*Value64 = Value32;
		break;

	case 64:
		EfiStatus = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint64, PciRegister, 1, (VOID *)Value64);
		break;

	default:
		return (AE_BAD_PARAMETER);
	}

	return (EFI_ERROR(EfiStatus)) ? (AE_ERROR) : (AE_OK);
}


/******************************************************************************
*
* FUNCTION:    AcpiOsWritePciConfiguration
*
* PARAMETERS:  PciId               - Seg/Bus/Dev
*              PciRegister         - Device Register
*              Value               - Value to be written
*              Width               - Number of bits
*
* RETURN:      Status.
*
* DESCRIPTION: Write data to PCI configuration space
*
*****************************************************************************/

ACPI_STATUS
AcpiOsWritePciConfiguration(
	ACPI_PCI_ID             *PciId,
	UINT32                  PciRegister,
	UINT64                  Value64,
	UINT32                  Width)
{
	EFI_PCI_IO_PROTOCOL         *PciIo;
	EFI_STATUS         EfiStatus;
	UINT8                   Value8;
	UINT16                  Value16;
	UINT32                  Value32;


	PciIo = AcpiEfiGetPciDev(PciId);
	if (PciIo == NULL)
	{
		return (AE_NOT_FOUND);
	}

	switch (Width)
	{
	case 8:
		Value8 = (UINT8)Value64;
		EfiStatus = PciIo->Pci.Write(PciIo, EfiPciIoWidthUint8, PciRegister, 1, (VOID *)&Value8);
		break;

	case 16:
		Value16 = (UINT16)Value64;
		EfiStatus = PciIo->Pci.Write(PciIo, EfiPciIoWidthUint16, PciRegister, 1, (VOID *)&Value16);
		break;

	case 32:
		Value32 = (UINT32)Value64;
		EfiStatus = PciIo->Pci.Write(PciIo, EfiPciIoWidthUint32, PciRegister, 1, (VOID *)&Value32);
		break;

	case 64:
		EfiStatus = PciIo->Pci.Write(PciIo, EfiPciIoWidthUint64, PciRegister, 1, (VOID *)&Value64);
		break;

	default:
		return (AE_BAD_PARAMETER);
	}

	return (EFI_ERROR(EfiStatus)) ? (AE_ERROR) : (AE_OK);
}

ACPI_STATUS
AcpiOsReadMemory(
	ACPI_PHYSICAL_ADDRESS   Address,
	UINT64                  *Value,
	UINT32                  Width)
{
	switch (Width)
	{
	case 8:
		*Value = *(UINT8*)Address;
		break;
	case 16:
		*Value = *(UINT16*)Address;
		break;
	case 32:
		*Value = *(UINT32*)Address;
		break;
	case 64:
		*Value = *(UINT64*)Address;
		break;
	default:
		return AE_BAD_VALUE;
	}
	return AE_OK;
}

ACPI_STATUS
AcpiOsWriteMemory(
	ACPI_PHYSICAL_ADDRESS Address,
	UINT64 Value,
	UINT32 Width)
{
	switch (Width)
	{
	case 8:
		*(UINT8*)Address = Value;
		break;
	case 16:
		*(UINT16*)Address = Value;
		break;
	case 32:
		*(UINT32*)Address = Value;
		break;
	case 64:
		*(UINT64*)Address = Value;
		break;
	default:
		return AE_BAD_VALUE;
	}
	return AE_OK;
}

ACPI_STATUS
AcpiOsReadPort(
	ACPI_IO_ADDRESS Address,
	UINT32 *Value,
	UINT32 Width)
{
	switch (Width)
	{
	case 8:
		*Value = inportb(Address);
		break;
	case 16:
		*Value = inportw(Address);
		break;
	case 32:
		*Value = inportd(Address);
		break;
	default:
		return AE_BAD_VALUE;
	}
	return AE_OK;
}

ACPI_STATUS
AcpiOsWritePort(
	ACPI_IO_ADDRESS Address,
	UINT32 Value,
	UINT32 Width)
{
	switch (Width)
	{
	case 8:
		outportb(Address, Value);
		break;
	case 16:
		outportw(Address, Value);
		break;
	case 32:
		outportd(Address, Value);
		break;
	default:
		return AE_BAD_VALUE;
	}
	return AE_OK;
}

UINT64 AcpiOsGetTimer()
{
	EFI_RUNTIME_SERVICES* RS = GetRuntimeServices();
	EFI_TIME time; EFI_TIME_CAPABILITIES cap;
	RS->GetTime(&time, &cap);
	return time.Nanosecond/100 + (time.Second+time.Minute*60+time.Hour*3600+time.Day*86400)*10000000;
}

void AcpiOsWaitEventsComplete()
{

}

}
