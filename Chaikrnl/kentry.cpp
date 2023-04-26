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
#include <PerformanceTest.h>
#include <string.h>
#include <kdraw.h>
#include <pciexpress.h>
#include <scheduler.h>
#include <usb.h>
#include <endian.h>
#include <lwip/netifapi.h>
#include <lwip/api.h>
#include <vds.h>
#include <vfs.h>
#include <EASTL/hash_map.h>

#define CHAIOS_KERNEL_VERSION_MAJOR 0
#define CHAIOS_KERNEL_VERSION_MINOR 9

#include <float.h>

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

static void print_guid(GUID& id)
{
	uint64_t part5 = 0;
	for (int i = 0; i < 6; ++i)
	{
		part5 <<= 8;
		part5 |= id.Data5[i];
	}
	kprintf_a("%x-%x-%x-%x-%lx", id.Data1, id.Data2, id.Data3, BE_TO_CPU16(id.Data4), part5);
}

static vds_enum_result vds_enum(HDISK disk)
{
	auto params = VdsGetParams(disk);
	if (params->parent != NULL)
		kprintf(u"  ");
	uint64_t megabytes = (params->diskSize * params->sectorSize) / (1024 * 1024);
	if (megabytes > 10 * 1024 * 1024)
		kprintf(u"  Disk %d: %d sectors, sector size %d, total size: %d TB, type ", disk, params->diskSize, params->sectorSize, megabytes / (1024*1024));
	else if(megabytes > 10*1024)
		kprintf(u"  Disk %d: %d sectors, sector size %d, total size: %d GB, type ", disk, params->diskSize, params->sectorSize, megabytes / 1024);
	else
		kprintf(u"  Disk %d: %d sectors, sector size %d, total size: %d MB, type ", disk, params->diskSize, params->sectorSize, megabytes);

	print_guid(params->diskType);
	kprintf(u", id ");
	print_guid(params->diskId);
	kprintf(u"\n");

	return RESULT_NOTBOUND;
}

static void network_outputter(void* param)
{
	semaphore_t sleepysem = create_semaphore(0, u"Network Output Wait Sem");

	auto connout = netconn_new(NETCONN_UDP);
	
	const ip_addr_t broadcast = IPADDR4_INIT(IPADDR_BROADCAST);
	//err_t err = netconn_connect(connout, &broadcast, 3120);

	const char* teststr = "ChaiOS";
	netbuf* buf = netbuf_new();
	size_t len = 0;
	for (; teststr[len]; ++len);
	//netbuf_ref(buf, teststr, len + 1);
	while (true)
	{
		void* buff = netbuf_alloc(buf, len + 1);
		memcpy(buff, teststr, len + 1);
		netconn_sendto(connout, buf, &broadcast, 3120);
		wait_semaphore(sleepysem, 1, 1000);

		netbuf_free(buf);
	}
	netbuf_delete(buf);
}

static PKERNEL_BOOT_INFO bootinf = nullptr;
EXTERN PKERNEL_BOOT_INFO getBootInfo()
{
	return bootinf;
}

EXTERN void user_function();

void dfsTree(size_t indent, HFILE curdir)
{
	size_t offset = 0;
	VFS_DIRECTORY_ENTRY direntry;
	for (;; ++offset)
	{
		ssize_t count = VfsReadFile(curdir, &direntry, sizeof(direntry), offset, nullptr);
		if (count > 0)
		{
			kprintf(u"Error reading directory\n");
			continue;
		}
		else if (count == 0)
			break;		//End of directory

		for (unsigned i = 0; i < indent; ++i)
			kprintf(u"  ");
		if (direntry.attributes & VFS_ATTR_DIRECTORY)
		{
			kprintf(u"Directory %s\n", direntry.filename);
			if (direntry.filename[0] == 'M')
				continue;
			HFILE dir = VfsOpenFile(curdir, direntry.filename, OPEN_MODE_READ, nullptr);
			dfsTree(indent + 1, dir);
		}
		else
		{
			kprintf(u"File %s\n", direntry.filename);
		}
	}
};

extern PerformanceElement gputs_performance;
extern PerformanceElement copyWindow_performance;

static void DisplayPerf(PerformanceElement& perfel, uint64_t elapsed)
{
	double InGputs = ((double)perfel.Counter / (double)elapsed) * 100;
	int Percent = (int)InGputs;
	int Fraction = (int)((InGputs - Percent) * 100);
	kprintf(u"%s Perfomance: %d.%d2%%\n", perfel.Name, Percent, Fraction);
}

extern bool CallConstructors();


void _kentry(PKERNEL_BOOT_INFO bootinfo)
{

	set_stdio_puts((chaios_stdio_puts_proc)bootinfo->puts_proc, NULL);
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
	SetBackgroundColour(RGB(0, 0, 0xFF));
	//STDIO now uses internal graphics
	PerformanceTest::GetPerformance().StartTiming();
	bootinf = bootinfo = copyBootInfo(bootinfo);
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
	kputs(u"mulitprocessor init\n");
	arch_setup_interrupts();
	//Scheduler is now running
	//startup_acpi();
	startup_multiprocessor();
	//Welcome to the thunderdome
	StartupGraphics();
	void* wnd = CreateStdioWindow(900, 700);		//Double buffer the entire screen
	//startup_acpi();
	setup_usb();
	//Start Virtual Disk System
	init_vds();

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

	eastl::hash_map<int, void*> test();
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

	//create_thread(network_outputter, nullptr, THREAD_PRIORITY_NORMAL, KERNEL_TASK);
	
	initialize_pci_drivers();
	usb_run();
	vfsInit();
	vds_start_filesystem_matching();
#if 0
	kprintf(u"VDS Information:\n");
	enumerate_disks(&vds_enum);

	kprintf(u"VFS Information:\n");
	char16_t drive[] = u"\0:\\";
	while (drive[0] = VfsListRootVolumes(drive[0]))		//Yes, assignment
	{
		kprintf(u"Drive %s\n", drive);
#if 0
		HFILE root = VfsOpenFile(NULL, drive, VFS_OPEN_MODES::OPEN_MODE_READ, nullptr);
		if(root)
			dfsTree(1, root);
#endif
	}
#endif
	auto elapsed = PerformanceTest::GetPerformance().StopTiming();
	DisplayPerf(gputs_performance, elapsed);
	DisplayPerf(copyWindow_performance, elapsed);

	kputs(u"System timer: ");
	//Test usermode
#if 1
	void* ufn = find_free_paging(PAGESIZE, (void*)0x1000000000);
	if (!paging_map(ufn, PADDR_ALLOCATE, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE))
	{
		kprintf(u"Failed to map at %x\n", ufn);
	}
	else
	{
		memcpy(ufn, &user_function, PAGESIZE);
		set_paging_attributes(ufn, PAGESIZE, PAGE_ATTRIBUTE_USER, PAGE_ATTRIBUTE_WRITABLE);
		void* stack = arch_init_stackptr(arch_create_stack(0, 1), 0);
		arch_go_usermode(stack, (void(*)(void*))ufn, 64);
	}
#endif

	while (1);
}