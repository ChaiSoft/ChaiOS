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
	ap_routine entryfunc;
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
	arch_setup_interrupts();
	while (1)
	{
		cpu_status_t stat = acquire_spinlock(comms->spinlock);
		ap_routine f = comms->entryfunc;
		void* dat = comms->data;
		comms->entryfunc = nullptr;
		comms->data = nullptr;
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
	bool enabled;
	stack_t init_stack;
};
typedef RedBlackTree<uint32_t, processor_info*> cputree_t;
static cputree_t cputree;

static processor_info* create_info(size_t cpuidt, size_t acpid)
{
	processor_info* info = new processor_info;
	cpu_communication* comms = new cpu_communication;
	info->comms = comms;
	info->acpi_id = acpid;
	comms->spinlock = create_spinlock();
	comms->entryfunc = 0;
	comms->data = 0;

	if (cpuidt != arch_current_processor_id())
	{
		if (stack_t stack = arch_create_stack(0, 0))
		{
			comms->stack = (size_t)arch_init_stackptr(stack, 0);
			info->init_stack = stack;
		}
		else
		{
			kprintf(u"Stack allocation for CPU %d failed\r\n", cpuidt);
			comms->stack = 0;
		}
	}
	return info;
}

static void sync_cpu(uint32_t cpuid, volatile cpu_data* data)
{
	//return;
	processor_info* info = cputree[cpuid];
	volatile cpu_communication* comms = info->comms;
	data->rendezvous = 0;
	if (!arch_startup_cpu(cpuid, (void*)addrtrampoline, &data->rendezvous, (size_t)comms))
	{
		kprintf(u"CPU %d wouldn't wake: %x\r\n", cpuid, data->rendezvous);
		delete_spinlock(comms->spinlock);
		arch_destroy_stack(info->init_stack, 0);
		return;
	}
	kprintf(u"Waiting for CPU %d Rendezvous: ", cpuid);
	const int timeout = 1000;
	for (int i = 0; i <= timeout && data->rendezvous; ++i)
		if (i == timeout) return kputs(u"failed\n");	

	kputs(u"Done\n");
	//Now send stack

	//CPU now has its comms area and stack
	//now tell it the entry point
	if (!arch_cas((size_t*)&comms->entryfunc, 0, (size_t)&ap_startup_routine))
		kputs(u"CAS failed\n");
	if (!arch_cas((size_t*)&comms->data, 0, (size_t)comms))
		kputs(u"CAS failed\n");
}

static void get_cpu_count(ACPI_TABLE_MADT* madt, size_t* numcpus, size_t* numenabled)
{
	for (auto it = cputree.begin(); it != cputree.end(); ++it)
	{
		++*numcpus;
		if (it->second->enabled)
			++*numenabled;
		else
			kprintf(u"Disabled CPU with ID %d\n", it->first);
	}
}

static void fill_cputree(ACPI_TABLE_MADT* madt)
{
	ACPI_SUBTABLE_HEADER* madtsubs = mem_after<ACPI_SUBTABLE_HEADER*>(madt);
	while ((char*)madtsubs - (char*)madt < madt->Header.Length)
	{
		if (madtsubs->Type == ACPI_MADT_TYPE_LOCAL_X2APIC)
		{
			ACPI_MADT_LOCAL_X2APIC* lapic = (ACPI_MADT_LOCAL_X2APIC*)madtsubs;
			if (cputree.find(lapic->LocalApicId) == cputree.end())
			{
				processor_info* inf = create_info(lapic->LocalApicId, lapic->Uid);
				inf->enabled = ((lapic->LapicFlags & ACPI_MADT_ENABLED) != 0);
				cputree[lapic->LocalApicId] = inf;
			}
		}
		else if (madtsubs->Type == ACPI_MADT_TYPE_LOCAL_APIC)
		{
			ACPI_MADT_LOCAL_APIC* lapic = (ACPI_MADT_LOCAL_APIC*)madtsubs;
			if (cputree.find(lapic->Id) == cputree.end())
			{
				processor_info* inf = create_info(lapic->Id, lapic->ProcessorId);
				inf->enabled = ((lapic->LapicFlags & ACPI_MADT_ENABLED) != 0);
				cputree[lapic->Id] = inf;
			}
		}
		madtsubs = raw_offset<ACPI_SUBTABLE_HEADER*>(madtsubs, madtsubs->Length);
	}
}

static RedBlackTree<size_t, size_t> cpuArchToLogical;
static RedBlackTree<size_t, size_t> cpuLogicalToArch;
static size_t logicalIdentAllocator = 0;

extern "C" size_t x64_read_cr3();

void startup_multiprocessor()
{
	//enable_screen_locking();
	//Count CPUs
	ACPI_TABLE_MADT* madt = nullptr;
	if (!ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt)))
		return;

	//Count the CPUs
	size_t numcpus = 0; size_t enabledcpus = 0;

	fill_cputree(madt);
	get_cpu_count(madt, &numcpus, &enabledcpus);
	kprintf(u"%d CPUs, %d enabled\r\n", numcpus, enabledcpus);
	static const size_t SECOND = 1000000;
	{
		//Got to use APIC
		//Prepare landing pad
		volatile cpu_data* data = (cpu_data*)0x1000;
		data->pagingbase = (void*)x64_read_cr3();
		data->bitness = BITS;

		memcpy((void*)addrtrampoline, (void*)ap_startup, sizeof(ap_startup));

		for (auto it = cputree.begin(); it != cputree.end(); ++it)
		{
			if (it->first == arch_current_processor_id())
			{
				continue;
			}
			sync_cpu(it->first, data);
			size_t logicalIdent = logicalIdentAllocator++;
			cpuArchToLogical[it->first] = logicalIdent;
			cpuLogicalToArch[logicalIdent] = it->first;
		}
	}
	
	//printf(u"%d CPUs successfully started up\r\n", cputree.size());
}

void ap_run_routine(uint32_t cpuid, ap_routine routine, void* data)
{
	auto comms = cputree[cpuid]->comms;
	auto stat = acquire_spinlock(comms->spinlock);
	comms->data = data;
	comms->entryfunc = routine;
	release_spinlock(comms->spinlock, stat);
}

void iterate_aps(ap_callback callback)
{
	for (auto it = cputree.begin(); it != cputree.end(); ++it)
	{
		if (it->first == arch_current_processor_id())
			continue;
		if (callback(it->first))
			break;
	}
}

EXTERN size_t CpuCount()
{
	return cputree.size();
}

EXTERN CHAIKRNL_FUNC size_t CpuCurrentLogicalId()
{
	return CpuIdentLogical(arch_current_processor_id());
}

EXTERN CHAIKRNL_FUNC size_t CpuIdentLogical(size_t archIdent)
{
	return cpuArchToLogical[archIdent];
}
EXTERN CHAIKRNL_FUNC size_t CpuIdentArchitectural(size_t logicalIdent)
{
	return cpuLogicalToArch[logicalIdent];
}