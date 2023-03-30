#include "acpihelp.h"
#include <kstdio.h>
#include <acpi.h>
#include <arch/cpu.h>
#include <arch/paging.h>
#include <spinlock.h>
#include <liballoc.h>

#include <chaikrnl.h>
#include <stdheaders.h>
#include <pciexpress.h>
#include <scheduler.h>
#include <semaphore.h>

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
	ACPI_STATUS Status;
	Status = AcpiInitializeSubsystem();
	if (ACPI_FAILURE(Status))
	{
		kprintf(u"Could not initialize ACPI: %d\n", Status);
		return;
	}
	AcpiInitializeTables(0, 0, FALSE);
}

void startup_acpi()
{
	ACPI_STATUS Status;
	Status = AcpiLoadTables();
	if (ACPI_FAILURE(Status))
	{
		kprintf(u"Could not load tables: %d\n", Status);
		return;
	}
	AcpiInstallInterface("ChaiOS");
	AcpiInstallInterface("Windows 2015");
	AcpiInstallInterface("Windows 2016");
	AcpiInstallInterface("Windows 2017");
	AcpiInstallInterface("Windows 2017.2");
	AcpiInstallInterface("Windows 2018");
	AcpiInstallInterface("Windows 2018.2");
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
	//Handle unaligned addresses
	size_t align_off = PhysicalAddress & (PAGESIZE - 1);
	void* loc = find_free_paging(Length + align_off);
	if (!paging_map(loc, PhysicalAddress - align_off, Length + align_off, PAGE_ATTRIBUTE_WRITABLE))
	{
		return nullptr;
	}
	return raw_offset<void*>(loc, align_off);
}
CHAIKRNL_FUNC void AcpiOsUnmapMemory(void *where, ACPI_SIZE length)
{
	paging_free(where, length, false);
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
	HTHREAD current = current_thread();
	return (ACPI_THREAD_ID)current;
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void *Context)
{
	if (!isscheduler())
		Function(Context);
	else
	{
		HTHREAD thread = create_thread(Function, Context, KERNEL_TASK);
		if (!thread)
			return AE_NO_MEMORY;
	}
	return AE_OK;
}

static void Stall(uint32_t microseconds)
{
	uint64_t current = AcpiOsGetTimer();
	while ((AcpiOsGetTimer() - current)/10 < microseconds);
}

CHAIKRNL_FUNC void AcpiOsSleep(UINT64 Milliseconds)
{
	kprintf(u"Sleeping for %d milliseconds\n", Milliseconds);
	Stall(Milliseconds * 1000);
}
CHAIKRNL_FUNC void AcpiOsStall(UINT32 Microseconds)
{
	kprintf(u"Stalling for %d milliseconds\n", Microseconds);
	Stall(Microseconds);
}

CHAIKRNL_FUNC ACPI_STATUS AcpiOsCreateMutex(ACPI_MUTEX *OutHandle)
{
	return AcpiOsCreateSemaphore(1, 1, OutHandle);
}
CHAIKRNL_FUNC void AcpiOsDeleteMutex(ACPI_MUTEX Handle)
{
	delete_semaphore(Handle);
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsAcquireMutex(ACPI_MUTEX Handle, UINT16 Timeout)
{
	return AcpiOsWaitSemaphore(Handle, 1, Timeout);
}
CHAIKRNL_FUNC void AcpiOsReleaseMutex(ACPI_MUTEX Handle)
{
	AcpiOsSignalSemaphore(Handle, 1);
}

CHAIKRNL_FUNC ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE *OutHandle)
{
	*OutHandle = create_semaphore(InitialUnits, u"AcpiSemaphore");
	if (!*OutHandle)
		return AE_NO_MEMORY;
	return AE_OK;
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle)
{
	delete_semaphore(Handle);
	return AE_OK;
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout)
{
	if (!Handle)
		return AE_BAD_PARAMETER;
	size_t tout = Timeout;
	if (Timeout == UINT16_MAX)
		tout = TIMEOUT_INFINITY;
	if (wait_semaphore(Handle, Units, tout) == 1)
		return AE_OK;
	else
		return AE_TIME;
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units)
{
	if (!Handle)
		return AE_BAD_PARAMETER;
	signal_semaphore(Handle, Units);
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

struct context_converter
{
	ACPI_OSD_HANDLER handler;
	void* ctxt;
};

static uint8_t interrupt_converter(size_t vector, void* param)
{
	kprintf(u"ACPI interrupt handler called\n");
	context_converter* ctx = (context_converter*)param;
	return ctx->handler(ctx->ctxt);
}

CHAIKRNL_FUNC ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler, void *Context)
{
	context_converter* cont = new context_converter;
	cont->handler = Handler;
	cont->ctxt = Context;
	arch_register_interrupt_handler(INTERRUPT_SUBSYSTEM_IRQ, InterruptLevel, INTERRUPT_CURRENTCPU, &interrupt_converter, cont);
	return AE_OK;
}
CHAIKRNL_FUNC ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER Handler)
{
	return AE_OK;
}

CHAIKRNL_FUNC void ACPI_INTERNAL_VAR_XFACE AcpiOsPrintf(const char *Format, ...)
{
	va_list args;
	va_start(args, Format);
	kvprintf_a(Format, args);
	va_end(args);
}

CHAIKRNL_FUNC void AcpiOsVprintf(const char *Format, va_list Args)
{
	kvprintf_a(Format, Args);
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
	return sys_timer() * 10000;
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
	bool result = read_pci_config(PciId->Segment, PciId->Bus, PciId->Device, PciId->Function, Reg, Width, Value);
	if (!result)
		return AE_NOT_IMPLEMENTED;
	return AE_OK;
}

CHAIKRNL_FUNC ACPI_STATUS
AcpiOsWritePciConfiguration(
	ACPI_PCI_ID             *PciId,
	UINT32                  Reg,
	UINT64                  Value,
	UINT32                  Width)
{
	bool result = write_pci_config(PciId->Segment, PciId->Bus, PciId->Device, PciId->Function, Reg, Width, Value);
	if (!result)
		return AE_NOT_IMPLEMENTED;
	return AE_OK;
}

}
