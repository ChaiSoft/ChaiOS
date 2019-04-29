#include <kernelinfo.h>
#include <arch/cpu.h>
#include <arch/paging.h>
#include <kstdio.h>
#include "uefihelper.h"
#include "pmmngr.h"
#include <liballoc.h>
#include <acpihelp.h>
#include <acpi.h>
#include <multiprocessor.h>
#include <string.h>
#include <kdraw.h>
#include <pciexpress.h>
#include <scheduler.h>
#include <usb.h>

#define CHAIOS_KERNEL_VERSION_MAJOR 0
#define CHAIOS_KERNEL_VERSION_MINOR 9

void* heapaddr = (void*)0xFFFFD40000000000;
static void* early_page_allocate(size_t numPages)
{
	if (!paging_map(heapaddr, PADDR_ALLOCATE, numPages*PAGESIZE, PAGE_ATTRIBUTE_WRITABLE))
	{
		heapaddr = find_free_paging(numPages*PAGESIZE, heapaddr);
		if (!paging_map(heapaddr, PADDR_ALLOCATE, numPages*PAGESIZE, PAGE_ATTRIBUTE_WRITABLE))
			return nullptr;
	}
	void* ret = heapaddr;
	heapaddr = raw_offset<void*>(heapaddr, numPages*PAGESIZE);
	return ret;
}

static int early_free_pages(void* p, size_t numpages)
{
	return 0;
}

PKERNEL_BOOT_INFO copyBootInfo(PKERNEL_BOOT_INFO inf)
{
	PKERNEL_BOOT_INFO newinf = new KERNEL_BOOT_INFO;
	memcpy(newinf, inf, sizeof(KERNEL_BOOT_INFO));
	return newinf;
}

ACPI_STATUS ProcessorWalker (
	ACPI_HANDLE Object,
	UINT32 NestingLevel,
	void *Context,
	void **ReturnValue)
{
	ACPI_BUFFER buffer;
	buffer.Pointer = new char[4096];
	buffer.Length = 4096;
	AcpiGetName(Object, ACPI_FULL_PATHNAME, &buffer);
	kprintf(u"Found processor object at %S\n", buffer.Pointer);
	ACPI_STATUS ptc = AcpiEvaluateObject(Object, "_PTC", NULL, &buffer);
	if (ACPI_SUCCESS(ptc))
	{
		kprintf(u" _PTC evaluated\n");
	}
	ACPI_STATUS pct = AcpiEvaluateObject(Object, "_PCT", NULL, &buffer);
	if (ACPI_SUCCESS(pct))
	{
		kprintf(u" _PCT evaluated\n");
	}
	return AE_OK;
}

static void* safe_early(size_t)
{
	return nullptr;
}

bool pci_print(uint16_t segment, uint16_t bus, uint16_t device)
{
	uint64_t result;
	read_pci_config(segment, bus, device, 0, 3, 32, &result);
	result = (result >> 16) & 0xFF;
	kprintf(u"Found PCI device at %d:%d:%d. Header type %d", segment, bus, device, result & 0x7F);
	if ((result & 0x80) != 0)
		kputs(u", multifunction\n");
	else
		kputs(u"\n");
	return false;
}

extern bool CallConstructors();
void _kentry(PKERNEL_BOOT_INFO bootinfo)
{
	set_stdio_puts(bootinfo->puts_proc);
	setLiballocAllocator(&safe_early, nullptr);
	kputs(u"CHAIOS KERNEL 0.09\n");
	//Begin hardware initialization
	arch_cpu_init();
	kputs(u"CPU startup completed\n");
	CallConstructors();
	//Now we need the ACPI tables for information on NUMA
	if (bootinfo->boottype == CHAIOS_BOOT_TYPE_UEFI)
	{
		kputs(u"UEFI boot\n");
		pull_system_table_data(bootinfo->efi_system_table);
	}
	//Now provide basic memory services
	initialize_pmmngr(bootinfo->pmmngr_info);
	paging_initialize(bootinfo->paging_info);
	setLiballocAllocator(&early_page_allocate, &early_free_pages);
	InitialiseGraphics(*bootinfo->fbinfo, bootinfo->kterm_status);
	set_stdio_puts(&gputs_k);
	//Copy boot information
	bootinfo = copyBootInfo(bootinfo);
	//ACPI Table Manager
	start_acpi_tables();
	//Now start up the PMMNGR properly
	startup_pmmngr(bootinfo->boottype, bootinfo->memory_map);
	initialize_pci_express();
	//Set up the VMMNGR
	//paging_boot_free();
	//We're now fully in the higher half and standalone
	arch_setup_interrupts();
	startup_multiprocessor();
	//Welcome to the thunderdome
	//startup_acpi();
	setup_usb();
	cpu_print_information();
	kprintf(u"Current CPU ID: %x\n", pcpu_data.cpuid);
	kputs(u"System timer: ");
	while (1)
	{
		uint64_t timer = arch_get_system_timer();
		kprintf(u"%d", timer);
		if(timer == 0)
			kputs(u"\b");
		for (;timer != 0; timer /= 10)
		{
			kputs(u"\b");
		}
	}
}