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
#include <endian.h>
#include <lwip/netifapi.h>
#include <lwip/api.h>

#define CHAIOS_KERNEL_VERSION_MAJOR 0
#define CHAIOS_KERNEL_VERSION_MINOR 9

void* heapaddr = (void*)0xFFFFD40000000000;
size_t heap_usage = 0;
static void* early_page_allocate(size_t numPages)
{
	volatile size_t* token = reinterpret_cast<volatile size_t*>(&heapaddr);
	size_t alloc_size = numPages * PAGESIZE;
	bool success = false;
	size_t tokval = *token;
	for (size_t count = 0; count < 10; ++count)
	{
		if ((success = arch_cas(token, tokval, tokval + alloc_size)))
			break;
		else
			tokval = *token;
	}
	if (!success)
	{
		kprintf(u"Allocation failed: contention\n");
		return nullptr;
	}
	void* ptr = (void*)tokval;

	if (!paging_map(ptr, PADDR_ALLOCATE, alloc_size, PAGE_ATTRIBUTE_WRITABLE))
	{
		return nullptr;
		//NOT MULTIPROCESSOR SAFE
		heapaddr = find_free_paging(numPages*PAGESIZE, heapaddr);
		if (!paging_map(heapaddr, PADDR_ALLOCATE, numPages*PAGESIZE, PAGE_ATTRIBUTE_WRITABLE))
			return nullptr;
	}
	
	heap_usage += alloc_size;
	return ptr;
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

void user_function(void*)
{
	while (1);
}

static netif* testnif;

EXTERN CHAIKRNL_FUNC void test_netif(netif* ptr)
{
	testnif = ptr;
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
	heap_usage = 0;
	//Now we need the ACPI tables for information on NUMA
	if (bootinfo->boottype == CHAIOS_BOOT_TYPE_UEFI)
	{
		kputs(u"UEFI boot\n");
		pull_system_table_data(bootinfo->efi_system_table);
	}
	//Now provide basic memory services
	kputs(u"PMMNGR init: ");
	initialize_pmmngr(bootinfo->pmmngr_info);
	kputs(u"complete\nPaging init: ");
	paging_initialize(bootinfo->paging_info);
	kputs(u"complete\nGraphics init: ");
	setLiballocAllocator(&early_page_allocate, &early_free_pages);
	InitialiseGraphics(*bootinfo->fbinfo, bootinfo->kterm_status);
	set_stdio_puts(&gputs_k);
	kputs(u"complete\nACPI init: ");
	//Copy boot information
	bootinfo = copyBootInfo(bootinfo);
	//ACPI Table Manager
	start_acpi_tables();
	kputs(u"complete\Pmmngr startup: ");
	//Now start up the PMMNGR properly
	startup_pmmngr(bootinfo->boottype, bootinfo->memory_map);
	kputs(u"complete\n");
	initialize_pci_express();
	//Set up the VMMNGR
	//paging_boot_free();
	//We're now fully in the higher half and standalone
	arch_setup_interrupts();
	startup_multiprocessor();
	//Welcome to the thunderdome
	//startup_acpi();
	//setup_usb();

	PIMAGE_DESCRIPTOR image = *reinterpret_cast<PIMAGE_DESCRIPTOR*>(bootinfo->modloader_info);
	while (image)
	{
		if (image->imageType == CHAIOS_BOOT_DRIVER)
		{
			kprintf_a("Boot Driver: %s\n", image->filename);
			image->EntryPoint(nullptr);
		}
		image = image->next;
	}
#if 0
	kstack_t ustack = arch_create_kernel_stack();
	void* ustackptr = arch_init_stackptr(ustack);
	set_paging_attributes(ustack, raw_diff(ustackptr, ustack), PAGE_ATTRIBUTE_USER, 0);
	void(*ufunc)(void*) = (void(*)(void*))0x10000000000;
	paging_map(ufunc, PADDR_ALLOCATE, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE);
	memcpy(ufunc, &user_function, PAGESIZE);
	set_paging_attributes(ufunc, PAGESIZE, PAGE_ATTRIBUTE_USER, PAGE_ATTRIBUTE_WRITABLE);
	arch_go_usermode(ustackptr, ufunc, sizeof(size_t)*8);
#endif

	uint64_be big_endian = CPU_TO_BE64(0xCAFEBABEDEADBEEF);
	kprintf(u"Big Endian value: %x, as little endian: %x\n", big_endian.v, BE_TO_CPU64(big_endian));
#if 0
	tls_slot_t x = AllocateKernelTls();
	kprintf(u"Allocated Kernel TLS: slot %d\n", x);
	tls_slot_t y = AllocateKernelTls();
	kprintf(u"Allocated Kernel TLS: slot %d\n", y);
	y = AllocateKernelTls();
	kprintf(u"Allocated Kernel TLS: slot %d\n", y);
	FreeKernelTls(x);
	y = AllocateKernelTls();
	kprintf(u"Freed slot %d, Allocated Kernel TLS: slot %d\n", x, y);
	cpu_print_information();
#endif
	kprintf(u"Heap Usage: %d KiB\n", heap_usage / (1024));
	kprintf(u"Current CPU ID: %x\n", pcpu_data.cpuid);

	kprintf(u"NETIF: %x\n", testnif);
	netconn* conn = netconn_new(NETCONN_UDP);
	err_t err = netconn_bind(conn, NULL, 3120);
	kprintf(u"conn: %x, err: %d\n", conn, err);

	while (1)
	{
		netbuf* buf = NULL;
		err = netconn_recv(conn, &buf);
		if (err)
			continue;
		kprintf(u"RECV called\n");
		void* data;
		uint16_t length;
		err= netbuf_data(buf, &data, &length);
		if (err)
			continue;
		for (size_t i = 0; i < length; ++i)
		{
			char16_t vals[2];
			vals[0] = ((char*)data)[i];
			vals[1] = 0;
			kputs(vals);
		}
	}
	

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