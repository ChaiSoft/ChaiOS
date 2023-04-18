#include <arch/paging.h>
#include <arch/cpu.h>
#include <kstdio.h>
#include "acpihelp.h"
#include <acpi.h>
#include <redblack.h>
#include <scheduler.h>

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_BSP 0x100 // Processor is a BSP
#define IA32_APIC_BASE_MSR_X2APIC 0x400
#define IA32_APIC_BASE_MSR_ENABLE 0x800

#define IA32_X2APIC_REGISTER_BASE_MSR 0x800

#define IA32_APIC_SVR_ENABLE 0x100

#define IA32_APIC_LVT_MASK 0x10000

#define LAPIC_REGISTER_ID 0x2
#define LAPIC_REGISTER_EOI 0xB
#define LAPIC_REGISTER_SVR 0xF
#define LAPIC_REGISTER_ICR 0x30
#define LAPIC_REGISTER_LVT_TIMER 0x32
#define LAPIC_REGISTER_LVT_ERROR 0x37

#define LAPIC_REGISTER_TMRINITCNT 0x38
#define LAPIC_REGISTER_TMRCURRCNT 0x39
#define LAPIC_REGISTER_TMRDIV 0x3E

extern "C" uint64_t x64_rdmsr(size_t msr);
extern "C" void x64_wrmsr(size_t msr, uint64_t);
extern "C" void x64_cpuid(size_t page, size_t* a, size_t* b, size_t* c, size_t* d, size_t subpage = 0);

static bool x2apic = false;
static void* apic = nullptr;

static uint64_t read_apic_register(uint16_t reg)
{
	if (x2apic)
	{
		size_t msr = IA32_X2APIC_REGISTER_BASE_MSR + reg;
		return x64_rdmsr(msr);
	}
	else
	{
		if (reg == LAPIC_REGISTER_ICR)
		{
			volatile uint32_t* reg_addr = raw_offset<volatile uint32_t*>(apic, reg << 4);
			volatile uint32_t* reg_next_addr = raw_offset<volatile uint32_t*>(apic, (reg+1) << 4);
			return *reg_addr | ((uint64_t)*reg_next_addr << 32);
		}
		volatile uint32_t* reg_addr = raw_offset<volatile uint32_t*>(apic, reg << 4);
		return *reg_addr;
	}
}

static void write_apic_register(uint16_t reg, uint64_t value)
{
	if (x2apic)
	{
		size_t msr = IA32_X2APIC_REGISTER_BASE_MSR + reg;
		x64_wrmsr(msr, value);
	}
	else
	{
		if (reg == LAPIC_REGISTER_ICR)
		{
			volatile uint32_t* reg_addr = raw_offset<volatile uint32_t*>(apic, reg << 4);
			volatile uint32_t* reg_next_addr = raw_offset<volatile uint32_t*>(apic, (reg + 1) << 4);
			uint32_t low_part = value & UINT32_MAX;
			uint32_t high_part = (value >> 32);
			*reg_next_addr = high_part;
			arch_memory_barrier();
			*reg_addr = low_part;
		}
		volatile uint32_t* reg_addr = raw_offset<volatile uint32_t*>(apic, reg << 4);
		*reg_addr = value;
	}
}
void apic_eoi()
{
	if(pcpu_data.irql >= IRQL_INTERRUPT)
		write_apic_register(LAPIC_REGISTER_EOI, 0);
}

void arch_local_eoi()
{
	if (pcpu_data.irql >= IRQL_INTERRUPT)
		write_apic_register(LAPIC_REGISTER_EOI, 0);
}

bool x2apic_supported()
{
	size_t a, b, c, d;
	x64_cpuid(0x1, &a, &b, &c, &d);
	return (c & (1 << 21)) != 0;
}

uint32_t arch_current_processor_id()
{
	size_t a, b, c, d;
	if (x2apic_supported())
	{
		x64_cpuid(0xB, &a, &b, &c, &d);
		return d;
	}
	else
	{
		x64_cpuid(0x1, &a, &b, &c, &d);
		return (b >> 24);
	}
}

static volatile uint64_t pit_ticks = 0;

uint64_t arch_get_system_timer()
{
	return pit_ticks;
}

static uint8_t apic_timer_interrupt(size_t vector, void* param)
{
	pcpu_data.cputicks = pcpu_data.cputicks + 1;
	scheduler_schedule(pit_ticks);
	scheduler_timer_tick();
	return 1;
}

static uint8_t pit_interrupt(size_t vector, void* param)
{
	++pit_ticks;

	return 1;
}

static uint8_t apic_spurious_interrupt(size_t vector, void* param)
{
	return 1;
}

struct ioapic_desc {
	uint32_t apic_id;
	uint32_t base_intr;
	uint32_t max_intr;
	void* mapped;
};

static RedBlackTree<uint32_t, ioapic_desc*> ioapics;
static RedBlackTree<uint32_t, ACPI_MADT_INTERRUPT_OVERRIDE*> interrupt_overrides;

#define IOAPIC_REG_ID 0x0
#define IOAPIC_REG_VER 0x1
#define IOAPIC_REG_ARB 0x2
#define IOAPIC_REG_RED_TBL_BASE 0x10

static uint32_t read_ioapic_register(void* apic_base, const uint8_t offset)
{
	volatile uint32_t* ioregsel = reinterpret_cast<volatile uint32_t*>(apic_base);
	volatile uint32_t* ioregwin = raw_offset<volatile uint32_t*>(apic_base, 0x10);
	//IOREGSEL
	*ioregsel = offset;
	return *ioregwin;
}

static void write_ioapic_register(void* apic_base, const uint8_t offset, const uint32_t val)
{
	volatile uint32_t* ioregsel = reinterpret_cast<volatile uint32_t*>(apic_base);
	volatile uint32_t* ioregwin = raw_offset<volatile uint32_t*>(apic_base, 0x10);
	//IOREGSEL
	*ioregsel = offset;
	*ioregwin = val;
}

static ioapic_desc* find_io_apic(uint32_t vector)
{
	RedBlackTree<uint32_t, ioapic_desc*>::iterator it = ioapics.near(vector);
	bool wasbigger = it->second->base_intr > vector;
	bool found = false;
	while (it != ioapics.end())
	{
		if (it->second->base_intr <= vector && it->second->base_intr + it->second->max_intr >= vector)
			return it->second;
		if (wasbigger)
		{
			if (it->second->base_intr < vector || it == ioapics.begin())
				break;
			--it;
		}
		else
		{
			if (it->second->base_intr > vector)
				break;
			++it;
		}
	}
	return nullptr;
}

#define APIC_VECTOR_BASE 0x50

static void init_ioapic(void* ioapic, ioapic_desc* desc)
{
	uint32_t ver = read_ioapic_register(ioapic, IOAPIC_REG_VER);
	uint32_t intr_num = (ver >> 16) & 0xFF;
	desc->max_intr = intr_num + desc->base_intr;
	arch_reserve_interrupt_range(desc->base_intr + APIC_VECTOR_BASE, desc->max_intr + APIC_VECTOR_BASE);
	for (size_t n = 0; n <= intr_num; ++n)
	{
		//Mask all interrupts
		uint32_t reg = IOAPIC_REG_RED_TBL_BASE + n * 2;
		write_ioapic_register(ioapic, reg, read_ioapic_register(ioapic, reg) | (1<<16));
	}
}

static void apic_register_irq(size_t vector, uint32_t processor, void* fn, void* param)
{
	//Convert IRQ number
	if (interrupt_overrides.find(vector) != interrupt_overrides.end())
		vector = interrupt_overrides[vector]->GlobalIrq;
	//Find relevant IO/APIC
	ioapic_desc* ioapic = find_io_apic(vector);
	if (!ioapic)
		return;
	uint32_t target_cpu = arch_current_processor_id();
	if (processor == INTERRUPT_CURRENTCPU)
	{
		processor = target_cpu;
	}
	else if (target_cpu != processor)
		return;		//TODO: IPI?
	uint32_t reg = IOAPIC_REG_RED_TBL_BASE + (vector - ioapic->base_intr) * 2;
	write_ioapic_register(ioapic->mapped, reg + 1, target_cpu << 24);
	//Install handler
	arch_register_interrupt_handler(INTERRUPT_SUBSYSTEM_DISPATCH, vector + APIC_VECTOR_BASE, target_cpu, fn, param);
	arch_install_interrupt_post_event(INTERRUPT_SUBSYSTEM_DISPATCH, vector + APIC_VECTOR_BASE, target_cpu, &apic_eoi);
	//Enable the interrupt
	write_ioapic_register(ioapic->mapped, reg, vector + APIC_VECTOR_BASE);
}
void apic_register_postevt(size_t vector, uint32_t processor, void(*evt)())
{
	//Unsupported
}
arch_interrupt_subsystem apic_subsystem{
	&apic_register_irq,
	&apic_register_postevt
};

static void EnableIOAPICsACPI()
{
	ACPI_TABLE_MADT* madt = nullptr;
	AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);
	if (!madt)
		return;
	ACPI_SUBTABLE_HEADER* subtab = mem_after<ACPI_SUBTABLE_HEADER*>(madt);
	typedef RedBlackTree<uint32_t, ACPI_SUBTABLE_HEADER*> ioapic_list;
	ioapic_list ioapic_parsing;
	while (raw_diff(subtab, madt) < madt->Header.Length)
	{
		if (subtab->Type == ACPI_MADT_TYPE_IO_APIC)
		{
			ACPI_MADT_IO_APIC* ioapic = reinterpret_cast<ACPI_MADT_IO_APIC*>(subtab);
			if (ioapic_parsing.find(ioapic->Id) == ioapic_parsing.end())		//Don't override IOSAPIC
				ioapic_parsing[ioapic->Id] = subtab;
		}
		else if (subtab->Type == ACPI_MADT_TYPE_IO_SAPIC)
		{
			ACPI_MADT_IO_SAPIC* ioapic = reinterpret_cast<ACPI_MADT_IO_SAPIC*>(subtab);
			ioapic_parsing[ioapic->Id] = subtab;
				
		}
		else if (subtab->Type == ACPI_MADT_TYPE_INTERRUPT_OVERRIDE)
		{
			ACPI_MADT_INTERRUPT_OVERRIDE* overrid = reinterpret_cast<ACPI_MADT_INTERRUPT_OVERRIDE*>(subtab);
			interrupt_overrides[overrid->SourceIrq] = overrid;
		}
		subtab = raw_offset<ACPI_SUBTABLE_HEADER*>(subtab, subtab->Length);
	}
	//We now have a list of IOAPICs
	for (ioapic_list::iterator it = ioapic_parsing.begin(); it != ioapic_parsing.end(); ++it)
	{
		void* ioapic_base = find_free_paging(PAGESIZE);
		ioapic_desc* desc = new ioapic_desc;
		desc->mapped = ioapic_base;
		desc->apic_id = it->first;
		desc->max_intr = 0;		//Need to ask the APIC itself
		switch (it->second->Type)
		{
		case ACPI_MADT_TYPE_IO_APIC:
			ACPI_MADT_IO_APIC* ioapic;
			ioapic = reinterpret_cast<ACPI_MADT_IO_APIC*>(it->second);
			desc->mapped = ioapic_base = raw_offset<void*>(ioapic_base, ioapic->Address & (PAGESIZE-1));
			paging_map(ioapic_base, ioapic->Address, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
			desc->base_intr = ioapic->GlobalIrqBase;
			ioapics[ioapic->GlobalIrqBase] = desc;
			break;
		case ACPI_MADT_TYPE_IO_SAPIC:
			ACPI_MADT_IO_SAPIC* iosapic;
			iosapic = reinterpret_cast<ACPI_MADT_IO_SAPIC*>(it->second);
			desc->mapped = ioapic_base = raw_offset<void*>(ioapic_base, iosapic->Address & (PAGESIZE - 1));
			paging_map(ioapic_base, iosapic->Address, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
			desc->base_intr = iosapic->GlobalIrqBase;
			ioapics[iosapic->GlobalIrqBase] = desc;
			break;
		}
		init_ioapic(ioapic_base, desc);
	}
	//All IOAPICs are now mapped and initialized
}

static void initialize_pit(uint32_t frequency)
{
	arch_register_interrupt_handler(INTERRUPT_SUBSYSTEM_IRQ, 0, INTERRUPT_CURRENTCPU, &pit_interrupt, nullptr);
	uint32_t reload_value = 0;
	if (frequency >= 1193181)
	{
		frequency = 1193181;
		reload_value = 1;
	}
	else if (frequency <= 18)
	{
		frequency = 18;
		reload_value = 0x10000;
	}
	else
	{
		reload_value = 1193181 / frequency;
	}
	arch_write_port(0x43, 0x36, 8);
	arch_write_port(0x40, reload_value & 0xFF, 8);
	arch_write_port(0x40, (reload_value>>8) & 0xFF, 8);
}

static uint64_t icr_dest(uint32_t processor)
{
	if (x2apic)
		return ((uint64_t)processor << 32);
	else
		return ((uint64_t)processor << 56);
}

static bool icr_busy()
{
	return (read_apic_register(LAPIC_REGISTER_ICR) & (1 << 12)) != 0;
}

void Stall(uint32_t milliseconds)
{
	uint64_t current = arch_get_system_timer();
	while (arch_get_system_timer() - current < milliseconds);
}

uint8_t arch_startup_cpu(uint32_t processor, void* address, volatile size_t* rendezvous, size_t rval)
{
	//Send INIT IPI
	write_apic_register(LAPIC_REGISTER_ICR, icr_dest(processor) | 0x4500);
	
	while (icr_busy());
	//Startup IPI
	size_t startup_ipi = icr_dest(processor) | 0x4600 | ((size_t)address >> 12);
	write_apic_register(LAPIC_REGISTER_ICR, startup_ipi);
	while (icr_busy());
	Stall(10);
	if (!arch_cas(rendezvous, 1, rval))
	{
		write_apic_register(LAPIC_REGISTER_ICR, startup_ipi);
		while (icr_busy());
		Stall(100);
		if (!arch_cas(rendezvous, 1, rval))
		{
			return 0;
		}
	}
	return 1;
}

#define PIC1		0x20		/* IO base address for master PIC */
#define PIC2		0xA0		/* IO base address for slave PIC */
#define PIC1_COMMAND	PIC1
#define PIC1_DATA	(PIC1+1)
#define PIC2_COMMAND	PIC2
#define PIC2_DATA	(PIC2+1)

#define ICW1_ICW4	0x01		/* ICW4 (not) needed */
#define ICW1_SINGLE	0x02		/* Single (cascade) mode */
#define ICW1_INTERVAL4	0x04		/* Call address interval 4 (8) */
#define ICW1_LEVEL	0x08		/* Level triggered (edge) mode */
#define ICW1_INIT	0x10		/* Initialization - required! */

#define ICW4_8086	0x01		/* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO	0x02		/* Auto (normal) EOI */
#define ICW4_BUF_SLAVE	0x08		/* Buffered mode/slave */
#define ICW4_BUF_MASTER	0x0C		/* Buffered mode/master */
#define ICW4_SFNM	0x10		/* Special fully nested (not) */

static void io_wait()
{
	volatile size_t counter = 0;
	for (; counter < 1000; ++counter);
}

static void disable_pic()
{
	static const int offset1 = 0x30;
	static const int offset2 = 0x38;
	//Remap it as spurious IRQs can occur
	arch_write_port(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4, 8);  // starts the initialization sequence (in cascade mode)
	io_wait();
	arch_write_port(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4, 8);
	io_wait();
	arch_write_port(PIC1_DATA, offset1, 8);                 // ICW2: Master PIC vector offset
	io_wait();
	arch_write_port(PIC2_DATA, offset2, 8);                 // ICW2: Slave PIC vector offset
	io_wait();
	arch_write_port(PIC1_DATA, 4, 8);                       // ICW3: tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
	io_wait();
	arch_write_port(PIC2_DATA, 2, 8);                       // ICW3: tell Slave PIC its cascade identity (0000 0010)
	io_wait();

	arch_write_port(PIC1_DATA, ICW4_8086, 8);
	io_wait();
	arch_write_port(PIC2_DATA, ICW4_8086, 8);
	io_wait();

	arch_write_port(PIC2_DATA, 0xFF, 8);
	io_wait();
	arch_write_port(PIC2_DATA, 0xFF, 8);
	//Handle spurious IRQs
	arch_register_interrupt_handler(INTERRUPT_SUBSYSTEM_DISPATCH, offset1 + 7, INTERRUPT_CURRENTCPU, &apic_spurious_interrupt, nullptr);
	arch_register_interrupt_handler(INTERRUPT_SUBSYSTEM_DISPATCH, offset2 + 7, INTERRUPT_CURRENTCPU, &apic_spurious_interrupt, nullptr);
}

void x64_init_apic()
{
	//First of all, disable the PIC
	if (arch_is_bsp())
	{
		disable_pic();
		//The PIC is now disabled. Enable the LAPIC
		uint64_t apic_base = x64_rdmsr(IA32_APIC_BASE_MSR);
		//Check for x2apic
		if (x2apic_supported())
		{
			x2apic = true;
			apic_base |= IA32_APIC_BASE_MSR_X2APIC;
		}
		else
		{
			paddr_t abase = apic_base & (SIZE_MAX - 0xFFF);
			apic = find_free_paging(PAGESIZE);
			paging_map(apic, abase, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING | PAGE_ATTRIBUTE_NO_EXECUTE);
		}
		apic_base |= IA32_APIC_BASE_MSR_ENABLE;
		x64_wrmsr(IA32_APIC_BASE_MSR, apic_base);
	}
	else
		x64_wrmsr(IA32_APIC_BASE_MSR, x64_rdmsr(IA32_APIC_BASE_MSR) | IA32_APIC_BASE_MSR_ENABLE | (x2apic ? IA32_APIC_BASE_MSR_X2APIC : 0));
	//Write the spurious vector register
	arch_register_interrupt_handler(INTERRUPT_SUBSYSTEM_DISPATCH, 0xFF, INTERRUPT_CURRENTCPU, &apic_spurious_interrupt, nullptr);
	write_apic_register(LAPIC_REGISTER_SVR, read_apic_register(LAPIC_REGISTER_SVR) | IA32_APIC_SVR_ENABLE | 0xFF);
	//The APIC is now enabled
	//Mask all local interrupts
	for (unsigned n = 0; n < LAPIC_REGISTER_LVT_ERROR - LAPIC_REGISTER_LVT_TIMER; ++n)
	{
		write_apic_register(LAPIC_REGISTER_LVT_TIMER + n, read_apic_register(LAPIC_REGISTER_LVT_TIMER + n) | IA32_APIC_LVT_MASK);
	}
	//Enable LAPIC timer
#if 1
	write_apic_register(LAPIC_REGISTER_TMRDIV, 0b1010);		//Divide by 128
	size_t timer_vector = 0x40;
	arch_register_interrupt_handler(INTERRUPT_SUBSYSTEM_DISPATCH, timer_vector, INTERRUPT_CURRENTCPU, &apic_timer_interrupt, nullptr);
	arch_install_interrupt_post_event(INTERRUPT_SUBSYSTEM_DISPATCH, timer_vector, INTERRUPT_CURRENTCPU, &apic_eoi);
	size_t tmrreg = (0b01 << 17) | timer_vector;
	write_apic_register(LAPIC_REGISTER_LVT_TIMER, tmrreg);
	write_apic_register(LAPIC_REGISTER_TMRINITCNT, 0x2000);
#endif
	//
	if (arch_is_bsp())
	{
		set_acpi_timer(&arch_get_system_timer);
		//Enable IOAPICs
		EnableIOAPICsACPI();
		arch_register_interrupt_subsystem(INTERRUPT_SUBSYSTEM_IRQ, &apic_subsystem);
		//Enable PIT
		initialize_pit(1000);
		scheduler_init(&apic_eoi);
	}
}