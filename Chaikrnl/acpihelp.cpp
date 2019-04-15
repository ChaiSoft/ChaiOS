#include "acpihelp.h"
#include <kstdio.h>
#include <acpi.h>
#include <arch/cpu.h>
#include "spinlock.h"
#include <liballoc.h>

#include <chaikrnl.h>

static void* RSDP = 0;
static acpi_system_timer sys_timer = nullptr;

void set_rsdp(void* rsdp)
{
	RSDP = rsdp;
}

void set_acpi_timer(acpi_system_timer timer)
{
	sys_timer = timer;
}

void start_acpi_tables()
{
	AcpiInitializeTables(0, 0, FALSE);
}

void startup_acpi()
{
	ACPI_STATUS Status;
	Status = AcpiInitializeSubsystem();
	if (ACPI_FAILURE(Status))
	{
		kprintf(u"Could not initialize ACPI: %d\n", Status);
		return;
	}
	Status = AcpiLoadTables();
	if (ACPI_FAILURE(Status))
	{
		kprintf(u"Could not load tables: %d\n", Status);
		return;
	}
	Status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
	if (ACPI_FAILURE(Status))
	{
		kprintf(u"Could not enable ACPI: %d\n", Status);
		return;
	}
	/* Complete the ACPI namespace object initialization */
	Status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
	if (ACPI_FAILURE(Status))
	{
		kprintf(u"Could not load objects: %d\n", Status);
		return;
	}
}

extern "C" {

CHAIKRNL_FUNC ACPI_STATUS AcpiOsInitialize()
{
	return AE_OK;
}

CHAIKRNL_FUNC ACPI_STATUS AcpiOsTerminate()
{
	return AE_OK;
}

CHAIKRNL_FUNC ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer()
{
	if (!RSDP)
	{
		AcpiFindRootPointer((ACPI_PHYSICAL_ADDRESS*)&RSDP);
	}
	return (ACPI_PHYSICAL_ADDRESS)RSDP;
}

CHAIKRNL_FUNC ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *PredefinedObject, ACPI_STRING *NewValue)
{
	*NewValue = 0;
	return AE_OK;
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER *ExistingTable, ACPI_TABLE_HEADER **NewTable)
{
	*NewTable = 0;
	return AE_OK;
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER *ExistingTable, ACPI_PHYSICAL_ADDRESS *NewAddress, UINT32 *NewTableLength)
{
	*NewAddress = 0;
	*NewTableLength = 0;
	return AE_OK;
}
CHAIKRNL_FUNC void *AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS PhysicalAddress, ACPI_SIZE Length)
{
	return (void*)PhysicalAddress;
}
CHAIKRNL_FUNC void AcpiOsUnmapMemory(void *where, ACPI_SIZE length)
{
	
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsGetPhysicalAddress(void *LogicalAddress, ACPI_PHYSICAL_ADDRESS *PhysicalAddress)
{
	*PhysicalAddress = (ACPI_PHYSICAL_ADDRESS)LogicalAddress;
	return AE_OK;
}
CHAIKRNL_FUNC void *AcpiOsAllocate(ACPI_SIZE Size)
{
	return kmalloc((size_t)Size);
}
CHAIKRNL_FUNC void AcpiOsFree(void *Memory)
{
	return kfree(Memory);
}
CHAIKRNL_FUNC BOOLEAN AcpiOsReadable(void *Memory, ACPI_SIZE Length)
{
	return TRUE;
}
CHAIKRNL_FUNC BOOLEAN AcpiOsWritable(void *Memory, ACPI_SIZE Length)
{
	return TRUE;
}
CHAIKRNL_FUNC ACPI_THREAD_ID AcpiOsGetThreadId()
{
	return 0;
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void *Context)
{
	Function(Context);
	return AE_OK;
}
CHAIKRNL_FUNC void AcpiOsSleep(UINT64 Milliseconds)
{
	//Stall(Milliseconds * 1000);
}
CHAIKRNL_FUNC void AcpiOsStall(UINT32 Microseconds)
{
	//Stall(Microseconds);
}

CHAIKRNL_FUNC ACPI_STATUS AcpiOsCreateMutex(ACPI_MUTEX *OutHandle)
{
	return AE_OK;
}
CHAIKRNL_FUNC void AcpiOsDeleteMutex(ACPI_MUTEX Handle)
{

}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsAcquireMutex(ACPI_MUTEX Handle, UINT16 Timeout)
{
	return AE_OK;
}
CHAIKRNL_FUNC void AcpiOsReleaseMutex(ACPI_MUTEX Handle)
{

}

CHAIKRNL_FUNC ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE *OutHandle)
{
	return AE_OK;
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle)
{
	return AE_OK;
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout)
{
	return AE_OK;
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units)
{
	return AE_OK;
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle)
{
	*OutHandle = create_spinlock();
	return AE_OK;
}
CHAIKRNL_FUNC void AcpiOsDeleteLock(ACPI_SPINLOCK Handle)
{
	delete_spinlock(Handle);
}
CHAIKRNL_FUNC ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK Handle)
{
	return acquire_spinlock(Handle);
}
CHAIKRNL_FUNC void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags)
{
	release_spinlock(Handle, Flags);
}

CHAIKRNL_FUNC ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler, void *Context)
{
	return AE_OK;
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER Handler)
{
	return AE_OK;
}

CHAIKRNL_FUNC void ACPI_INTERNAL_VAR_XFACE AcpiOsPrintf(const char *Format, ...)
{
	
}

CHAIKRNL_FUNC void AcpiOsVprintf(const char *Format, va_list Args)
{

}

CHAIKRNL_FUNC int AcpiOsAcquireGlobalLock(UINT32 *lock)
{
	return AE_OK;
}
CHAIKRNL_FUNC int AcpiOsReleaseGlobalLock(UINT32 *lock)
{
	return AE_OK;
}

CHAIKRNL_FUNC ACPI_STATUS AcpiOsSignal(UINT32 Function, void *Info)
{
	return AE_NOT_FOUND;
}

CHAIKRNL_FUNC ACPI_STATUS
AcpiOsEnterSleep(
	UINT8                   SleepState,
	UINT32                  RegaValue,
	UINT32                  RegbValue)
{
	return AE_NOT_IMPLEMENTED;
}

CHAIKRNL_FUNC ACPI_STATUS
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

CHAIKRNL_FUNC ACPI_STATUS
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

CHAIKRNL_FUNC ACPI_STATUS
AcpiOsReadPort(
	ACPI_IO_ADDRESS Address,
	UINT32 *Value,
	UINT32 Width)
{
	switch (Width)
	{
	case 8:
	case 16:
	case 32:
		*Value = arch_read_port(Address, Width);
		break;
	default:
		return AE_BAD_VALUE;
	}
	return AE_OK;
}

CHAIKRNL_FUNC ACPI_STATUS
AcpiOsWritePort(
	ACPI_IO_ADDRESS Address,
	UINT32 Value,
	UINT32 Width)
{
	switch (Width)
	{
	case 8:
	case 16:
	case 32:
		arch_write_port(Address, Value, Width);
		break;
	default:
		return AE_BAD_VALUE;
	}
	return AE_OK;
}

CHAIKRNL_FUNC UINT64 AcpiOsGetTimer()
{
	return sys_timer();
}

CHAIKRNL_FUNC void AcpiOsWaitEventsComplete()
{
}

CHAIKRNL_FUNC ACPI_STATUS
AcpiOsReadPciConfiguration(
	ACPI_PCI_ID             *PciId,
	UINT32                  Reg,
	UINT64                  *Value,
	UINT32                  Width)
{
	return AE_NOT_IMPLEMENTED;
}

CHAIKRNL_FUNC ACPI_STATUS
AcpiOsWritePciConfiguration(
	ACPI_PCI_ID             *PciId,
	UINT32                  Reg,
	UINT64                  Value,
	UINT32                  Width)
{
	return AE_NOT_IMPLEMENTED;
}

}
