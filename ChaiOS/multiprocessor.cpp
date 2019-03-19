#include "multiprocessor.h"
#include "liballoc.h"
#include "uefilib.h"
#include "kterm.h"
#include <acpi.h>

#define EFI_MP_SERVICES_PROTOCOL_GUID \
  { \
    0x3fdda605, 0xa76e, 0x4f46, {0xad, 0x29, 0x12, 0xf4, 0x53, 0x1b, 0x3d, 0x08} \
}
#define END_OF_CPU_LIST 0xffffffff
#define PROCESSOR_AS_BSP_BIT 0x00000001
#define PROCESSOR_ENABLED_BIT 0x00000002
#define PROCESSOR_HEALTH_STATUS_BIT 0x00000004

typedef
VOID
(EFIAPI *EFI_AP_PROCEDURE)(
	IN OUT VOID  *Buffer
	);

typedef struct _EFI_MP_SERVICES_PROTOCOL EFI_MP_SERVICES_PROTOCOL;

typedef struct {
	///
	/// Zero-based physical package number that identifies the cartridge of the processor.
	///
	UINT32  Package;
	///
	/// Zero-based physical core number within package of the processor.
	///
	UINT32  Core;
	///
	/// Zero-based logical thread number within core of the processor.
	///
	UINT32  Thread;
} EFI_CPU_PHYSICAL_LOCATION;

typedef struct {
	///
	/// The unique processor ID determined by system hardware.  For IA32 and X64,
	/// the processor ID is the same as the Local APIC ID. Only the lower 8 bits
	/// are used, and higher bits are reserved.  For IPF, the lower 16 bits contains
	/// id/eid, and higher bits are reserved.
	///
	UINT64                     ProcessorId;
	///
	/// Flags indicating if the processor is BSP or AP, if the processor is enabled
	/// or disabled, and if the processor is healthy. Bits 3..31 are reserved and
	/// must be 0.
	///
	/// <pre>
	/// BSP  ENABLED  HEALTH  Description
	/// ===  =======  ======  ===================================================
	///  0      0       0     Unhealthy Disabled AP.
	///  0      0       1     Healthy Disabled AP.
	///  0      1       0     Unhealthy Enabled AP.
	///  0      1       1     Healthy Enabled AP.
	///  1      0       0     Invalid. The BSP can never be in the disabled state.
	///  1      0       1     Invalid. The BSP can never be in the disabled state.
	///  1      1       0     Unhealthy Enabled BSP.
	///  1      1       1     Healthy Enabled BSP.
	/// </pre>
	///
	UINT32                     StatusFlag;
	///
	/// The physical location of the processor, including the physical package number
	/// that identifies the cartridge, the physical core number within package, and
	/// logical thread number within core.
	///
	EFI_CPU_PHYSICAL_LOCATION  Location;
} EFI_PROCESSOR_INFORMATION;

typedef
EFI_STATUS
(EFIAPI *EFI_MP_SERVICES_GET_NUMBER_OF_PROCESSORS)(
	IN  EFI_MP_SERVICES_PROTOCOL  *This,
	OUT UINTN                     *NumberOfProcessors,
	OUT UINTN                     *NumberOfEnabledProcessors
	);

typedef
EFI_STATUS
(EFIAPI *EFI_MP_SERVICES_GET_PROCESSOR_INFO)(
	IN  EFI_MP_SERVICES_PROTOCOL   *This,
	IN  UINTN                      ProcessorNumber,
	OUT EFI_PROCESSOR_INFORMATION  *ProcessorInfoBuffer
	);

typedef
EFI_STATUS
(EFIAPI *EFI_MP_SERVICES_STARTUP_ALL_APS)(
	IN  EFI_MP_SERVICES_PROTOCOL  *This,
	IN  EFI_AP_PROCEDURE          Procedure,
	IN  BOOLEAN                   SingleThread,
	IN  EFI_EVENT                 WaitEvent               OPTIONAL,
	IN  UINTN                     TimeoutInMicroSeconds,
	IN  VOID                      *ProcedureArgument      OPTIONAL,
	OUT UINTN                     **FailedCpuList         OPTIONAL
	);

typedef
EFI_STATUS
(EFIAPI *EFI_MP_SERVICES_STARTUP_THIS_AP)(
	IN  EFI_MP_SERVICES_PROTOCOL  *This,
	IN  EFI_AP_PROCEDURE          Procedure,
	IN  UINTN                     ProcessorNumber,
	IN  EFI_EVENT                 WaitEvent               OPTIONAL,
	IN  UINTN                     TimeoutInMicroseconds,
	IN  VOID                      *ProcedureArgument      OPTIONAL,
	OUT BOOLEAN                   *Finished               OPTIONAL
	);

typedef
EFI_STATUS
(EFIAPI *EFI_MP_SERVICES_SWITCH_BSP)(
	IN EFI_MP_SERVICES_PROTOCOL  *This,
	IN  UINTN                    ProcessorNumber,
	IN  BOOLEAN                  EnableOldBSP
	);

typedef
EFI_STATUS
(EFIAPI *EFI_MP_SERVICES_ENABLEDISABLEAP)(
	IN  EFI_MP_SERVICES_PROTOCOL  *This,
	IN  UINTN                     ProcessorNumber,
	IN  BOOLEAN                   EnableAP,
	IN  UINT32                    *HealthFlag OPTIONAL
	);

typedef
EFI_STATUS
(EFIAPI *EFI_MP_SERVICES_WHOAMI)(
	IN EFI_MP_SERVICES_PROTOCOL  *This,
	OUT UINTN                    *ProcessorNumber
	);

struct _EFI_MP_SERVICES_PROTOCOL {
	EFI_MP_SERVICES_GET_NUMBER_OF_PROCESSORS  GetNumberOfProcessors;
	EFI_MP_SERVICES_GET_PROCESSOR_INFO        GetProcessorInfo;
	EFI_MP_SERVICES_STARTUP_ALL_APS           StartupAllAPs;
	EFI_MP_SERVICES_STARTUP_THIS_AP           StartupThisAP;
	EFI_MP_SERVICES_SWITCH_BSP                SwitchBSP;
	EFI_MP_SERVICES_ENABLEDISABLEAP           EnableDisableAP;
	EFI_MP_SERVICES_WHOAMI                    WhoAmI;
};

struct cpu_data {
	size_t bitness;
	size_t rendezvous;
	void* pagingbase;
	spinlock_t (*create_spinlock)();
	cpu_status_t(*acquire_spinlock)(spinlock_t lock);
	void(*release_spinlock)(spinlock_t lock, cpu_status_t status);
	void(*delete_spinlock)(spinlock_t lock);
};

struct cpu_communication {
	spinlock_t spinlock;
	size_t stack;
	void(*entryfunc)(void*);
	void* data;
};

void EFIAPI ap_startup_routine(void* data)
{
	while (1);
}

VOID EFIAPI notifyevt(EFI_EVENT Event, VOID* Context)
{

}

void write_apic(void* addr, uint32_t reg, uint32_t val)
{
	uint32_t volatile* apicregs = (uint32_t volatile*)addr;
	apicregs[reg * 4] = val;
	memory_barrier();
}
uint32_t read_apic(void* addr, uint32_t reg)
{
	uint32_t volatile* apicregs = (uint32_t volatile*)addr;
	uint32_t val = apicregs[reg * 4];
	return val;
}

#if defined(X86) || defined(X64)
static const unsigned char ap_startup[] = {
	0xFA, 0xFC, 0xEA, 0x07, 0xA0, 0x00, 0x00, 0x31, 0xC0, 0x8E, 0xD8, 0x0F, 0x01, 0x16, 0x98, 0xA1,
	0x0F, 0x20, 0xC0, 0x66, 0x83, 0xC8, 0x01, 0x0F, 0x22, 0xC0, 0xEA, 0x1F, 0xA0, 0x08, 0x00, 0x66,
	0xB8, 0x10, 0x00, 0x8E, 0xD8, 0x8E, 0xC0, 0x8E, 0xD0, 0x83, 0x3D, 0x00, 0x10, 0x00, 0x00, 0x40,
	0x74, 0x15, 0xA1, 0x08, 0x10, 0x00, 0x00, 0x0F, 0x22, 0xD8, 0x0F, 0x20, 0xC0, 0x0D, 0x00, 0x00,
	0x00, 0x80, 0x0F, 0x22, 0xC0, 0xEB, 0xFE, 0xBF, 0x00, 0x20, 0x00, 0x00, 0x0F, 0x22, 0xDF, 0x31,
	0xC0, 0xB9, 0x00, 0x10, 0x00, 0x00, 0xF3, 0xAB, 0xBF, 0x00, 0x20, 0x00, 0x00, 0xC7, 0x07, 0x03,
	0x30, 0x00, 0x00, 0xC7, 0x47, 0x04, 0x00, 0x00, 0x00, 0x00, 0x66, 0x81, 0xC7, 0x00, 0x10, 0xC7,
	0x07, 0x03, 0x40, 0x00, 0x00, 0xC7, 0x47, 0x04, 0x00, 0x00, 0x00, 0x00, 0x66, 0x81, 0xC7, 0x00,
	0x10, 0xC7, 0x07, 0x03, 0x50, 0x00, 0x00, 0xC7, 0x47, 0x04, 0x00, 0x00, 0x00, 0x00, 0x66, 0x81,
	0xC7, 0x00, 0x10, 0xB8, 0x03, 0x00, 0x00, 0x00, 0xB9, 0x00, 0x02, 0x00, 0x00, 0x89, 0x07, 0x05,
	0x00, 0x10, 0x00, 0x00, 0x83, 0xC7, 0x08, 0xE2, 0xF4, 0x0F, 0x20, 0xE0, 0x83, 0xC8, 0x20, 0x0F,
	0x22, 0xE0, 0xB9, 0x80, 0x00, 0x00, 0xC0, 0x0F, 0x32, 0x0D, 0x00, 0x01, 0x00, 0x00, 0x0F, 0x30,
	0x0F, 0x20, 0xC0, 0x0D, 0x00, 0x00, 0x00, 0x80, 0x0F, 0x22, 0xC0, 0x0F, 0x01, 0x15, 0xB8, 0xA1,
	0x00, 0x00, 0xEA, 0xD9, 0xA0, 0x00, 0x00, 0x08, 0x00, 0x66, 0xB8, 0x10, 0x00, 0x8E, 0xD8, 0x8E,
	0xC0, 0x8E, 0xD0, 0x48, 0x8B, 0x04, 0x25, 0x10, 0x10, 0x00, 0x00, 0x0F, 0x22, 0xD8, 0xB9, 0x01,
	0x00, 0x00, 0x00, 0xB8, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x48, 0x0F, 0xB1, 0x0C, 0x25, 0x08, 0x10,
	0x00, 0x00, 0x75, 0xEF, 0x48, 0x39, 0x0C, 0x25, 0x08, 0x10, 0x00, 0x00, 0x74, 0xF6, 0x48, 0x8B,
	0x14, 0x25, 0x08, 0x10, 0x00, 0x00, 0x48, 0xC7, 0x04, 0x25, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0xB9, 0x00, 0x00, 0x00, 0x00, 0xB8, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x48, 0x0F, 0xB1,
	0x4A, 0x08, 0x74, 0xF8, 0x48, 0x89, 0xC4, 0x48, 0x8B, 0x0A, 0x48, 0x8B, 0x1C, 0x25, 0x20, 0x10,
	0x00, 0x00, 0x49, 0x89, 0xD4, 0x4C, 0x8B, 0x2C, 0x25, 0x28, 0x10, 0x00, 0x00, 0x49, 0x89, 0xCE,
	0xFF, 0xD3, 0x49, 0x83, 0x7C, 0x24, 0x10, 0x00, 0x75, 0x0D, 0x48, 0x89, 0xC2, 0x4C, 0x89, 0xF1,
	0x41, 0xFF, 0xD5, 0xF3, 0x90, 0xEB, 0xE9, 0x48, 0x89, 0xC2, 0x4C, 0x89, 0xF1, 0x41, 0xFF, 0xD5,
	0x49, 0x8B, 0x54, 0x24, 0x10, 0x49, 0x8B, 0x4C, 0x24, 0x18, 0xFF, 0xD2, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00,
	0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00, 0x17, 0x00, 0x80, 0xA1, 0x00, 0x00, 0x90, 0x90,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9A, 0xAF, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00, 0x17, 0x00, 0xA0, 0xA1, 0x00, 0x00,
};
#endif

#ifdef X86
#define BITS 32
#elif defined X64
#define BITS 64
#else
#error "Unknown processor architecture"
#endif

void startup_multiprocessor()
{
	//Count CPUs
	ACPI_TABLE_MADT* madt = nullptr;
	if (!ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt)))
		return;
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
	static const size_t SECOND = 1000000;
	//Try to use UEFI startup
	EFI_BOOT_SERVICES* BS = GetBootServices();
	if (BS)
	{
		EFI_GUID mpprotg = EFI_MP_SERVICES_PROTOCOL_GUID;
		EFI_MP_SERVICES_PROTOCOL* mpprot;
		if (EFI_SUCCESS(BS->LocateProtocol(&mpprotg, NULL, (void**)&mpprot)))
		{
			EFI_EVENT evt;
			BS->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_APPLICATION, &notifyevt, NULL, &evt);
			UINTN* failedcpus = nullptr;
			mpprot->StartupAllAPs(mpprot, &ap_startup_routine, FALSE, evt, 0, NULL, &failedcpus);
			puts(u"Successfully started all processors\r\n");
			return;
		}
	}
	Stall(1 * SECOND);
	//Got to use APIC
	//Prepare landing pad
	cpu_data* data = (cpu_data*)0x1000;
	data->pagingbase = get_paging_root();
	data->acquire_spinlock = &acquire_spinlock;
	data->create_spinlock = &create_spinlock;
	data->release_spinlock = &release_spinlock;
	data->delete_spinlock = &delete_spinlock;
	data->bitness = BITS;

	size_t addrtrampoline = 0xA000;
	BS->CopyMem((void*)addrtrampoline, (void*)ap_startup, sizeof(ap_startup));

	void* apic = (void*)madt->Address;
	madtsubs = (ACPI_SUBTABLE_HEADER*)&madt[1];
	while ((char*)madtsubs - (char*)madt < madt->Header.Length)
	{
		if (madtsubs->Type == ACPI_MADT_TYPE_LOCAL_APIC)
		{
			ACPI_MADT_LOCAL_APIC* lapic = (ACPI_MADT_LOCAL_APIC*)madtsubs;
			if (lapic->LapicFlags & ACPI_MADT_ENABLED)
			{
				//Current CPU so we don't startup the BSP
				uint32_t apicid = read_apic(apic, 0x2) >> 24;
				if (lapic->Id != apicid)
				{
					printf(u"Starting CPU with ID %d (ACPI: %d)\r\n", lapic->Id, lapic->ProcessorId);
					data->rendezvous = 0;
					//INIT
					write_apic(apic, 0x31, lapic->Id << 24);
					write_apic(apic, 0x30, 0x4500);
					//Startup
					while (read_apic(apic, 0x30) & (1 << 12));
					write_apic(apic, 0x31, lapic->Id << 24);
					write_apic(apic, 0x30, 0x4600 | (addrtrampoline >> 12));
					Stall(10000);
					cpu_communication* comms = new cpu_communication;
					comms->spinlock = create_spinlock();
					comms->stack = 0;
					if (!compareswap(&data->rendezvous, 1, (size_t)comms))
					{
						write_apic(apic, 0x31, lapic->Id << 24);
						write_apic(apic, 0x30, 0x4600 | (addrtrampoline >> 12));
						data->rendezvous = (size_t)comms;
						Stall(100000);
						if (!compareswap(&data->rendezvous, 1, (size_t)comms))
						{
							delete_spinlock(comms->spinlock);
							delete comms;
							goto cont;
						}
					}
					Stall(1);
					while(*(size_t volatile*)&data->rendezvous);
					//CPU now has its comms area, give it a stack
					EFI_PHYSICAL_ADDRESS stack = 0;
					const size_t PAGES_STACK = 16;
					if (EFI_SUCCESS(BS->AllocatePages(AllocateAnyPages, EfiBootServicesData, PAGES_STACK, &stack)))
					{
						stack += EFI_PAGE_SIZE*PAGES_STACK - sizeof(void*);		//Stack grows downwards
						compareswap(&comms->stack, 0, stack);
						//now tell it the entry point
						cpu_status_t stat = acquire_spinlock(comms->spinlock);
						comms->entryfunc = &ap_startup_routine;
						comms->data = nullptr;
						release_spinlock(comms->spinlock, stat);
						printf(u"CPU %d started up\r\n", lapic->Id);
					}
				}
			cont:
				;
			}
		}
		madtsubs = (ACPI_SUBTABLE_HEADER*)((char*)madtsubs + madtsubs->Length);
	}
}