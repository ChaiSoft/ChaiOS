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

#define CHAIOS_KERNEL_VERSION_MAJOR 0
#define CHAIOS_KERNEL_VERSION_MINOR 9

void* heapaddr = (void*)0xFFFFD40000000000;
static void* early_page_allocate(size_t numPages)
{
	if (!paging_map(heapaddr, PADDR_ALLOCATE, numPages*PAGESIZE, PAGE_ATTRIBUTE_WRITABLE))
		return nullptr;
	void* ret = heapaddr;
	heapaddr = raw_offset<void*>(heapaddr, numPages*PAGESIZE);
	return ret;
}

static int early_free_pages(void* p, size_t numpages)
{
	return 0;
}

extern bool CallConstructors();
void _kentry(PKERNEL_BOOT_INFO bootinfo)
{
	set_stdio_puts(bootinfo->puts_proc);
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
	//ACPI Table Manager
	start_acpi_tables();
	//Now start up the PMMNGR properly
	startup_pmmngr(bootinfo->boottype, bootinfo->memory_map);
	//Set up the VMMNGR

	
	cpu_print_information();
	while (1);
}