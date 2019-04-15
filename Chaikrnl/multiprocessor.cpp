#include <multiprocessor.h>
#include <liballoc.h>
#include <kstdio.h>
#include <redblack.h>
#include <spinlock.h>
#include <acpi.h>
#include <arch/cpu.h>
#include <arch/paging.h>
#include <string.h>
#include <stdheaders.h>

#pragma pack (push, 1)
struct cpu_data {
	size_t bitness;
	size_t rendezvous;
	void* pagingbase;
};
#pragma pack(pop)

#pragma pack (push, 1)
struct cpu_communication {
	spinlock_t spinlock;
	size_t stack;
	void(*entryfunc)(void*);
	void* data;
	uint16_t segregs[8];
	uint8_t gdt_entry[sizeof(size_t)+2];
};
#pragma pack(pop)

void ap_startup_routine(void* data)
{
	arch_cpu_init();
	volatile cpu_communication* comms = (volatile cpu_communication*)data;
	comms->entryfunc = nullptr;
	comms->data = nullptr;
	while (1)
	{
		cpu_status_t stat = acquire_spinlock(comms->spinlock);
		void(*f)(void*) = comms->entryfunc;
		void* dat = nullptr;
		release_spinlock(comms->spinlock, stat);
		if (f != nullptr)
		{
			f(dat);
		}
		arch_pause();
	}
}

void write_apic(void* addr, uint32_t reg, uint32_t val)
{
	uint32_t volatile* apicregs = (uint32_t volatile*)addr;
	apicregs[reg * 4] = val;
	arch_memory_barrier();
}
uint32_t read_apic(void* addr, uint32_t reg)
{
	uint32_t volatile* apicregs = (uint32_t volatile*)addr;
	uint32_t val = apicregs[reg * 4];
	return val;
}

#if defined(X86) || defined(X64)
static const unsigned char ap_startup[] = {
	0xFA, 0xFC, 0xEA, 0x07, 0xA0, 0x00, 0x00, 0x31, 0xC0, 0x8E, 0xD8, 0x0F, 0x01, 0x16, 0xA0, 0xA1,
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
	0x0F, 0x20, 0xC0, 0x0D, 0x00, 0x00, 0x00, 0x80, 0x0F, 0x22, 0xC0, 0x0F, 0x01, 0x15, 0xC0, 0xA1,
	0x00, 0x00, 0xEA, 0xD9, 0xA0, 0x00, 0x00, 0x08, 0x00, 0x66, 0xB8, 0x10, 0x00, 0x8E, 0xD8, 0x8E,
	0xC0, 0x8E, 0xD0, 0x48, 0x8B, 0x04, 0x25, 0x10, 0x10, 0x00, 0x00, 0x0F, 0x22, 0xD8, 0x48, 0x31,
	0xC9, 0x48, 0x31, 0xC0, 0xB9, 0x01, 0x00, 0x00, 0x00, 0xB8, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x48,
	0x0F, 0xB1, 0x0C, 0x25, 0x08, 0x10, 0x00, 0x00, 0xF3, 0x90, 0x75, 0xED, 0xB9, 0x01, 0x00, 0x00,
	0x00, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xF0, 0x48, 0x0F, 0xB1, 0x0C, 0x25, 0x08, 0x10, 0x00, 0x00,
	0xF3, 0x90, 0x74, 0xED, 0x49, 0x89, 0xC4, 0x48, 0x31, 0xC9, 0x48, 0x31, 0xC0, 0x48, 0x89, 0x04,
	0x25, 0x08, 0x10, 0x00, 0x00, 0xF0, 0x49, 0x0F, 0xB1, 0x4C, 0x24, 0x08, 0xF3, 0x90, 0x74, 0xF5,
	0x48, 0x89, 0xC4, 0x48, 0x31, 0xC9, 0x48, 0x31, 0xC0, 0xF0, 0x49, 0x0F, 0xB1, 0x4C, 0x24, 0x10,
	0xF3, 0x90, 0x74, 0xF5, 0x48, 0x31, 0xC0, 0xF0, 0x49, 0x0F, 0xB1, 0x4C, 0x24, 0x18, 0xF3, 0x90,
	0x74, 0xF5, 0x0F, 0x01, 0x04, 0x25, 0x00, 0x18, 0x00, 0x00, 0x49, 0x8B, 0x54, 0x24, 0x10, 0x49,
	0x8B, 0x4C, 0x24, 0x18, 0x66, 0x83, 0xE4, 0xF0, 0x48, 0x89, 0xE5, 0x48, 0x83, 0xEC, 0x20, 0x0F,
	0x01, 0x3C, 0x24, 0xFF, 0xE2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00,
	0x17, 0x00, 0x88, 0xA1, 0x00, 0x00, 0x90, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xAF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00,
	0x17, 0x00, 0xA8, 0xA1, 0x00, 0x00,
};

static const size_t addrtrampoline = 0xA000;
#endif

#ifdef X86
#define BITS 32
#elif defined X64
#define BITS 64
#else
#error "Unknown processor architecture"
#endif

struct processor_info {
	volatile cpu_communication* comms;
	uint32_t acpi_id;
};
typedef RedBlackTree<uint32_t, processor_info*> cputree_t;
cputree_t cputree;

static void Stall(uint32_t microseconds)
{
	uint64_t current = AcpiOsGetTimer();
	while (AcpiOsGetTimer() - current < microseconds * 10);
}

static processor_info* create_info(size_t cpuidt, size_t acpid)
{
	processor_info* info = new processor_info;
	cpu_communication* comms = new cpu_communication;
	info->comms = comms;
	info->acpi_id = acpid;
	comms->spinlock = create_spinlock();
	comms->entryfunc = 0;
	comms->data = 0;

	const size_t PAGES_STACK = 16;
	if (void* stack = kmalloc(PAGES_STACK*PAGESIZE))
	{
		stack = raw_offset<void*>(stack, PAGESIZE *PAGES_STACK - sizeof(void*));	//Stack grows downwards
		comms->stack = (size_t)stack;
		//printf(u"CPU %d allocated stack at %x\r\n", cpuidt, stack);
	}
	else
	{
		kprintf(u"Stack allocation for CPU %d failed\r\n", cpuidt);
		comms->stack = 0;
	}
	return info;
}

size_t find_apic(ACPI_TABLE_MADT* madt)
{
	size_t apic = madt->Address;
	ACPI_SUBTABLE_HEADER* madtsubs = (ACPI_SUBTABLE_HEADER*)&madt[1];
	while ((char*)madtsubs - (char*)madt < madt->Header.Length)
	{
		if (madtsubs->Type == ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE)
		{
			ACPI_MADT_LOCAL_APIC_OVERRIDE* overr = (ACPI_MADT_LOCAL_APIC_OVERRIDE*)madtsubs;
			apic = overr->Address;
			break;
		}
		madtsubs = (ACPI_SUBTABLE_HEADER*)((char*)madtsubs + madtsubs->Length);
	}
	return apic;
}

enum StartupMode {
	APIC,
	X2APIC
};

StartupMode get_startup_mode(ACPI_TABLE_MADT* madt)
{
	bool x2apic = false;
	ACPI_SUBTABLE_HEADER* madtsubs = (ACPI_SUBTABLE_HEADER*)&madt[1];
	while ((char*)madtsubs - (char*)madt < madt->Header.Length)
	{
		if (madtsubs->Type == ACPI_MADT_TYPE_LOCAL_X2APIC)
		{
			x2apic = true;
			break;
		}
		madtsubs = (ACPI_SUBTABLE_HEADER*)((char*)madtsubs + madtsubs->Length);
	}
	if (x2apic)
		return X2APIC;
	else
		return APIC;
}

static void apic_startup(ACPI_TABLE_MADT* madt, volatile cpu_data* data)
{
	void* apic = (void*)find_apic(madt);
	ACPI_SUBTABLE_HEADER* madtsubs = (ACPI_SUBTABLE_HEADER*)&madt[1];
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
					kprintf(u"Starting CPU with ID %d (ACPI: %d)\r\n", lapic->Id, lapic->ProcessorId);
					data->rendezvous = 0;
					processor_info* info = cputree[lapic->Id];
					volatile cpu_communication* comms = info->comms;
					//INIT
					write_apic(apic, 0x31, lapic->Id << 24);
					write_apic(apic, 0x30, 0x4500);
					//Startup
					while (read_apic(apic, 0x30) & (1 << 12));
					write_apic(apic, 0x31, lapic->Id << 24);
					write_apic(apic, 0x30, 0x4600 | (addrtrampoline >> 12));
					while (read_apic(apic, 0x30) & (1 << 12));
					Stall(10000);
					if (!arch_cas(&data->rendezvous, 1, (size_t)comms))
					{
						write_apic(apic, 0x31, lapic->Id << 24);
						write_apic(apic, 0x30, 0x4600 | (addrtrampoline >> 12));
						while (read_apic(apic, 0x30) & (1 << 12));
						Stall(100000);
						if (!arch_cas(&data->rendezvous, 1, (size_t)comms))
						{
							kprintf(u"CPU %d wouldn't wake: %x\r\n", lapic->Id, data->rendezvous);
							delete_spinlock(comms->spinlock);
							delete comms;
							info->comms = nullptr;
							goto cont;
						}
					}
					Stall(1);
					while (data->rendezvous);
					//CPU now has its comms area and stack
					//now tell it the entry point
					arch_cas((size_t*)&comms->entryfunc, 0, (size_t)&ap_startup_routine);
					arch_cas((size_t*)&comms->data, 0, (size_t)comms);
					//printf(u"CPU %d started up with stack %x, comms %x\r\n", lapic->Id, comms->stack, comms);
				}
			cont:
				;
			}
		}
		madtsubs = (ACPI_SUBTABLE_HEADER*)((char*)madtsubs + madtsubs->Length);
	}
}

extern "C" uint64_t x64_rdmsr(size_t msr);
extern "C" void x64_wrmsr(size_t msr, uint64_t);

static void x2apic_startup(ACPI_TABLE_MADT* madt, volatile cpu_data* data)
{
	ACPI_SUBTABLE_HEADER* madtsubs = (ACPI_SUBTABLE_HEADER*)&madt[1];
	//ENABLE X2APIC
	static const uint32_t IA32_APIC_BASE = 0x1B;
	static const uint32_t IA32_APIC_ID = 0x802;
	static const uint32_t IA32_APIC_ICR = 0x830;
	uint64_t x2amsr = x64_rdmsr(IA32_APIC_BASE);
	x2amsr |= (3 << 10);
	x64_wrmsr(IA32_APIC_BASE, x2amsr);

	//Current CPU so we don't startup the BSP
	uint32_t apicid = x64_rdmsr(IA32_APIC_ID);

	while ((char*)madtsubs - (char*)madt < madt->Header.Length)
	{
		if (madtsubs->Type == ACPI_MADT_TYPE_LOCAL_X2APIC)
		{
			ACPI_MADT_LOCAL_X2APIC* lapic = (ACPI_MADT_LOCAL_X2APIC*)madtsubs;
			if (lapic->LapicFlags & ACPI_MADT_ENABLED)
			{
				if (lapic->LocalApicId != apicid)
				{
					kprintf(u"x2APIC Starting CPU with ID %d (ACPI: %d)\r\n", lapic->LocalApicId, lapic->Uid);
					data->rendezvous = 0;
					processor_info* info = cputree[lapic->LocalApicId];
					volatile cpu_communication* comms = info->comms;
					//INIT
					uint64_t ipi = ((uint64_t)lapic->LocalApicId << 32) | 0x4500;
					x64_wrmsr(IA32_APIC_ICR, ipi);
					//Startup
					ipi = ((uint64_t)lapic->LocalApicId << 32) | 0x4600 | (addrtrampoline >> 12);
					x64_wrmsr(IA32_APIC_ICR, ipi);
					Stall(10000);
					if (!arch_cas(&data->rendezvous, 1, (size_t)comms))
					{
						x64_wrmsr(IA32_APIC_ICR, ipi);
						Stall(100000);
						if (!arch_cas(&data->rendezvous, 1, (size_t)comms))
						{
							kprintf(u"x2APIC CPU %d wouldn't wake: %x\r\n", lapic->LocalApicId, data->rendezvous);
							delete_spinlock(comms->spinlock);
							delete comms;
							info->comms = nullptr;
							goto cont;
						}
					}
					Stall(1);
					while (data->rendezvous);
					//CPU now has its comms area and stack
					//now tell it the entry point
					arch_cas((size_t*)&comms->entryfunc, 0, (size_t)&ap_startup_routine);
					arch_cas((size_t*)&comms->data, 0, (size_t)comms);
					//printf(u"CPU %d started up with stack %x, comms %x\r\n", lapic->Id, comms->stack, comms);
				}
			cont:
				;
			}
		}
		madtsubs = (ACPI_SUBTABLE_HEADER*)((char*)madtsubs + madtsubs->Length);
	}
}

static void get_cpu_count(StartupMode mode, ACPI_TABLE_MADT* madt, size_t* numcpus, size_t* numenabled)
{
	if (mode == APIC)
	{
		ACPI_SUBTABLE_HEADER* madtsubs = (ACPI_SUBTABLE_HEADER*)&madt[1];
		while ((char*)madtsubs - (char*)madt < madt->Header.Length)
		{
			if (madtsubs->Type == ACPI_MADT_TYPE_LOCAL_APIC)
			{
				ACPI_MADT_LOCAL_APIC* lapic = (ACPI_MADT_LOCAL_APIC*)madtsubs;
				++*numcpus;
				if (lapic->LapicFlags & ACPI_MADT_ENABLED)
				{
					//printf(u"Active CPU with ID %d (ACPI: %d)\r\n", lapic->Id, lapic->ProcessorId);
					++*numenabled;
				}
				else
				{
					kprintf(u"Disabled CPU with ID %d (ACPI: %d)\r\n", lapic->Id, lapic->ProcessorId);
				}
			}
			madtsubs = (ACPI_SUBTABLE_HEADER*)((char*)madtsubs + madtsubs->Length);
		}
	}
	else if (mode == X2APIC)
	{
		ACPI_SUBTABLE_HEADER* madtsubs = (ACPI_SUBTABLE_HEADER*)&madt[1];
		while ((char*)madtsubs - (char*)madt < madt->Header.Length)
		{
			if (madtsubs->Type == ACPI_MADT_TYPE_LOCAL_X2APIC)
			{
				ACPI_MADT_LOCAL_X2APIC* lapic = (ACPI_MADT_LOCAL_X2APIC*)madtsubs;
				++*numcpus;
				if (lapic->LapicFlags & ACPI_MADT_ENABLED)
				{
					//printf(u"Active CPU with ID %d (ACPI: %d)\r\n", lapic->Id, lapic->ProcessorId);
					++*numenabled;
				}
				else
				{
					kprintf(u"Disabled CPU with ID %d (ACPI: %d)\r\n", lapic->LocalApicId, lapic->Uid);
				}
			}
			madtsubs = (ACPI_SUBTABLE_HEADER*)((char*)madtsubs + madtsubs->Length);
		}
	}
}
static size_t lookup_cpu_acpid(StartupMode mode, ACPI_TABLE_MADT* madt, size_t id)
{
	ACPI_SUBTABLE_HEADER* madtsubs = (ACPI_SUBTABLE_HEADER*)&madt[1];
	while ((char*)madtsubs - (char*)madt < madt->Header.Length)
	{
		if (mode == APIC && madtsubs->Type == ACPI_MADT_TYPE_LOCAL_APIC)
		{
			ACPI_MADT_LOCAL_APIC* lapic = (ACPI_MADT_LOCAL_APIC*)madtsubs;
			if (lapic->Id == id)
				return lapic->ProcessorId;
		}
		else if (mode == X2APIC && madtsubs->Type == ACPI_MADT_TYPE_LOCAL_X2APIC)
		{
			ACPI_MADT_LOCAL_X2APIC* lapic = (ACPI_MADT_LOCAL_X2APIC*)madtsubs;
			if (lapic->LocalApicId == id)
				return lapic->Uid;
		}
		madtsubs = (ACPI_SUBTABLE_HEADER*)((char*)madtsubs + madtsubs->Length);
	}
	return -1;
}
static void fill_cputree(StartupMode mode, ACPI_TABLE_MADT* madt)
{
	if (mode == X2APIC)
	{
		ACPI_SUBTABLE_HEADER* madtsubs = (ACPI_SUBTABLE_HEADER*)&madt[1];
		while ((char*)madtsubs - (char*)madt < madt->Header.Length)
		{
			if (madtsubs->Type == ACPI_MADT_TYPE_LOCAL_X2APIC)
			{
				ACPI_MADT_LOCAL_X2APIC* lapic = (ACPI_MADT_LOCAL_X2APIC*)madtsubs;
				processor_info* inf = create_info(lapic->LocalApicId, lapic->Uid);
				cputree[lapic->LocalApicId] = inf;
			}
			madtsubs = (ACPI_SUBTABLE_HEADER*)((char*)madtsubs + madtsubs->Length);
		}
	}
	else if (mode == APIC)
	{
		ACPI_SUBTABLE_HEADER* madtsubs = (ACPI_SUBTABLE_HEADER*)&madt[1];
		while ((char*)madtsubs - (char*)madt < madt->Header.Length)
		{
			if (madtsubs->Type == ACPI_MADT_TYPE_LOCAL_APIC)
			{
				ACPI_MADT_LOCAL_APIC* lapic = (ACPI_MADT_LOCAL_APIC*)madtsubs;
				processor_info* inf = create_info(lapic->Id, lapic->ProcessorId);
				cputree[lapic->Id] = inf;
			}
			madtsubs = (ACPI_SUBTABLE_HEADER*)((char*)madtsubs + madtsubs->Length);
		}
	}
	else
	{
		kputs(u"Error: Unknown multiprocessor mode\r\n");
	}
}

extern "C" size_t x64_read_cr3();

void startup_multiprocessor()
{
	//Count CPUs
	ACPI_TABLE_MADT* madt = nullptr;
	if (!ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt)))
		return;
	size_t lapicaddr = find_apic(madt);
	kprintf(u"LAPIC address: %x\r\n", lapicaddr);

	//Count the CPUs
	size_t numcpus = 0; size_t enabledcpus = 0;

	StartupMode mode = get_startup_mode(madt);
	get_cpu_count(mode, madt, &numcpus, &enabledcpus);
	fill_cputree(mode, madt);
	
	kprintf(u"%d CPUs, %d enabled\r\n", numcpus, enabledcpus);
	static const size_t SECOND = 1000000;
	{
		//Got to use APIC
		//Prepare landing pad
		volatile cpu_data* data = (cpu_data*)0x1000;
		data->pagingbase = (void*)x64_read_cr3();
		data->bitness = BITS;

		memcpy((void*)addrtrampoline, (void*)ap_startup, sizeof(ap_startup));

		if (mode == APIC)
		{
			apic_startup(madt, data);
		}
		else if(mode == X2APIC)
		{
			x2apic_startup(madt, data);
		}
		else
		{
			kputs(u"Error: unknown multiprocessor startup mode\r\n");
		}
	}
	
	//printf(u"%d CPUs successfully started up\r\n", cputree.size());
}