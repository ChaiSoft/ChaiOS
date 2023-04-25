#include "pciexpress.h"
#include <acpi.h>
#include <arch/paging.h>
#include <arch/cpu.h>
#include <kstdio.h>
#include <redblack.h>

static ACPI_TABLE_MCFG* mcfg = nullptr;

#define LEGACY_CONFIG_ADDRESS 0xCF8
#define LEGACY_CONFIG_DATA 0xCFC

#define PCI_STATUS_CAPLIST (1<<4)

#define PCI_MEM_BAR_TYPE(x) \
((x >> 1) & 0x3)

#define PCI_IS_IO_BAR(x) \
((x & 1) != 0)

#define PCI_REG_ID 0x0
#define PCI_REG_COMSTAT 0x1
#define PCI_REG_CLASS 0x2
#define PCI_REG_HDRTYPE 0x3
#define PCI_REG_BAR0 0x4
#define PCI_REG_CAPPTR 0xD

#define PCI_CAP_ID_MSI 0x05
#define PCI_CAP_ID_MSIX 0x11

#define DEBUG 0

enum PCI_MEM_BAR_TYPES {
	MEMBAR32,
	RESERVED,
	MEMBAR64
};

static RedBlackTree<uint32_t, pci_register_callback> pci_by_vendor;
static RedBlackTree<uint32_t, pci_register_callback> pci_by_class;

static bool read_pci_legacy(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t* result)
{
	if (segment != 0)
		return false;
	if (bus > 255)
		return false;
	if (device > 31)
		return false;
	if (function > 7)
		return false;
	uint32_t address = (1 << 31) | (bus << 16) | (device << 11) | (function << 8) | (reg * 4);
	arch_write_port(LEGACY_CONFIG_ADDRESS, address, 32);
	uint32_t res = arch_read_port(LEGACY_CONFIG_DATA, 32);
	bool success = true;
	switch (width)
	{
	case 32:
		break;
	case 16:
		res &= UINT16_MAX;
		break;
	case 8:
		res &= UINT8_MAX;
		break;
	default:
		success = false;
	}
	*result = res;
	return success;
}

static bool write_pci_legacy(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t value)
{
	if (segment != 0)
		return false;
	if (bus > 255)
		return false;
	if (device > 31)
		return false;
	if (function > 7)
		return false;
	uint32_t address = (1 << 31) | (bus << 16) | (device << 11) | (function << 8) | (reg * 4);
	arch_write_port(LEGACY_CONFIG_ADDRESS, address, 32);
	uint32_t res = arch_read_port(LEGACY_CONFIG_DATA, 32);
	bool success = true;
	switch (width)
	{
	case 32:
		res = value;
		break;
	case 16:
		res = (res & 0xFFFF0000) | value;
		break;
	case 8:
		res = (res & 0xFFFFFF00) | value;
		break;
	default:
		success = false;
	}
	if (success)
	{
		arch_write_port(LEGACY_CONFIG_ADDRESS, address, 32);
		arch_write_port(LEGACY_CONFIG_DATA, res, 32);
	}
	return success;
}

static paddr_t find_pci_device(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function)
{
	if (bus > 255)
		return 0;
	if (device > 31)
		return 0;
	if (function > 7)
		return 0;
	ACPI_MCFG_ALLOCATION* allocs = mem_after<ACPI_MCFG_ALLOCATION*>(mcfg);
	for (; raw_diff(allocs, mcfg) < mcfg->Header.Length; ++allocs)
	{
		if (allocs->PciSegment == segment && allocs->StartBusNumber <= bus && bus <= allocs->EndBusNumber)
			break;
	}
	if (raw_diff(allocs, mcfg) >= mcfg->Header.Length)
		return 0;
	paddr_t paddr_page = allocs->Address + ((bus) << 20) | (device << 15) | (function << 12);
	return paddr_page;
}

static void unmap_config(void* addr)
{
	if(addr != nullptr && addr != (void*)SIZE_MAX)
		paging_free(addr, PAGESIZE, false);
}

static void* internal_read_pci(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t* result, void* mapped)
{
	if (!mcfg)
	{
		return read_pci_legacy(segment, bus, device, function, reg, width, result) ? (void*)SIZE_MAX : nullptr;
	}
	if (!mapped)
	{
		paddr_t dev = find_pci_device(segment, bus, device, function);
		if (dev == 0)
			return nullptr;
		mapped = find_free_paging(PAGESIZE);
		if (!paging_map(mapped, dev, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING))
		{
			kprintf(u"Error mapping PCI memory: %x, %x\n", mapped, dev);
			return nullptr;
		}
	}
	bool written = true;
	switch (width)
	{
	case 8:
		*result = *raw_offset<volatile uint8_t*>(mapped, reg * 4);
		break;
	case 16:
		*result = *raw_offset<volatile uint16_t*>(mapped, reg * 4);
		break;
	case 32:
		*result = *raw_offset<volatile uint32_t*>(mapped, reg * 4);
		break;
	case 64:
		*result = *raw_offset<volatile uint64_t*>(mapped, reg * 4);
		break;
	default:
		written = false;
	}
	if (!written)
		unmap_config(mapped);
	return written ? mapped : nullptr;
}

static void* internal_write_pci(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t value, void* mapped)
{
	if (!mcfg)
	{
		return write_pci_legacy(segment, bus, device, function, reg, width, value) ? (void*)SIZE_MAX : nullptr;
	}
	if (!mapped)
	{
		paddr_t dev = find_pci_device(segment, bus, device, function);
		if (dev == 0)
			return nullptr;
		void* mapped = find_free_paging(PAGESIZE);
		if (!paging_map(mapped, dev, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING))
		{
			kprintf(u"Error mapping PCI memory: %x, %x\n", mapped, dev);
			return nullptr;
		}
	}
	bool written = true;
	switch (width)
	{
	case 8:
		*raw_offset<volatile uint8_t*>(mapped, reg * 4) = value;
		break;
	case 16:
		*raw_offset<volatile uint16_t*>(mapped, reg * 4) = value;
		break;
	case 32:
		*raw_offset<volatile uint32_t*>(mapped, reg * 4) = value;
		break;
	case 64:
		*raw_offset<volatile uint64_t*>(mapped, reg * 4) = value;
		break;
	default:
		written = false;
	}
	if (!written)
		unmap_config(mapped);
	return written ? mapped : nullptr;
}

bool read_pci_config(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t* result)
{
	void* mapped = internal_read_pci(segment, bus, device, function, reg, width, result, nullptr);
	unmap_config(mapped);
	return mapped != nullptr;
}

bool write_pci_config(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t value)
{
	void* mapped = internal_write_pci(segment, bus, device, function, reg, width, value, nullptr);
	unmap_config(mapped);
	return mapped != nullptr;
}

void initialize_pci_express()
{
	AcpiGetTable(ACPI_SIG_MCFG, 0, (ACPI_TABLE_HEADER**)&mcfg);
	if (!mcfg)
		return;
}

uint16_t pci_get_vendor_id(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, PCI_REG_ID, 32, &result);
	return result & 0xFFFF;
}

uint16_t pci_get_device_id(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, PCI_REG_ID, 32, &result);
	return (result >> 16) & 0xFFFF;
}

uint32_t pci_get_vendordev(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, PCI_REG_ID, 32, &result);
	return result & UINT32_MAX;
}

uint8_t pci_get_header_type(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, PCI_REG_HDRTYPE, 32, &result);
	return (result >> 16) & 0xFF;
}

static uint16_t getSecondaryBus(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, 6, 32, &result);
	return (result >> 8) & 0xFF;
}

uint8_t pci_get_base_class(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, PCI_REG_CLASS, 32, &result);
	return (result >> 24) & 0xFF;
}

uint8_t pci_get_subclass(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, PCI_REG_CLASS, 32, &result);
	return (result >> 16) & 0xFF;
}

uint8_t pci_get_programming_interface(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, PCI_REG_CLASS, 32, &result);
	return (result >> 8) & 0xFF;
}

uint32_t pci_get_classcode(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, PCI_REG_CLASS, 32, &result);
	return (result >> 8) & 0xFFFFFF;
}

paddr_t read_pci_bar(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, size_t BAR, size_t* BARSIZE)
{
	uint64_t baseaddr, highbits, headertype;
	uint32_t bartype;
	void* internalptr = nullptr;
	internalptr = internal_read_pci(segment, bus, device, function, PCI_REG_HDRTYPE, 32, &headertype, internalptr);
	headertype >>= 16;
	headertype &= 0xFF;
	paddr_t ret = 0;
	if (((headertype & 0x7F) == 1 && BAR > 1) || ((headertype & 0x7F) == 2))
		goto end;
	if (BAR > 5)
		goto end;
	internalptr = internal_read_pci(segment, bus, device, function, BAR + PCI_REG_BAR0, 32, &baseaddr, internalptr);
	bartype = baseaddr & 0xF;
	if (PCI_IS_IO_BAR(bartype))
	{
		ret = baseaddr;		//I/O BAR
		goto end;
	}
	else if (PCI_MEM_BAR_TYPE(bartype) == MEMBAR32)
		ret = baseaddr ^ bartype;	//32 bit BAR
	else if (PCI_MEM_BAR_TYPE(bartype) == MEMBAR64)
	{
		//64 bit BAR
		internalptr = internal_read_pci(segment, bus, device, function, BAR + 1 + PCI_REG_BAR0, 32, &highbits, internalptr);
		baseaddr |= (highbits << 32);
		ret = baseaddr ^ bartype;
	}
	else
		goto end;
	if (BARSIZE)
	{
		//Work out size
		internalptr = internal_write_pci(segment, bus, device, function, BAR + PCI_REG_BAR0, 32, UINT32_MAX, internalptr);
		if(PCI_MEM_BAR_TYPE(bartype) == MEMBAR64)
			internalptr = internal_write_pci(segment, bus, device, function, BAR + 1 + PCI_REG_BAR0, 32, UINT32_MAX, internalptr);
		//Read back
		uint64_t szbits, szhighbits = 0;
		internalptr = internal_read_pci(segment, bus, device, function, BAR + PCI_REG_BAR0, 32, &szbits, internalptr);
		if (PCI_MEM_BAR_TYPE(bartype) == MEMBAR64)
			internalptr = internal_read_pci(segment, bus, device, function, BAR + 1 + PCI_REG_BAR0, 32, &szhighbits, internalptr);
		//Restore register
		internalptr = internal_write_pci(segment, bus, device, function, BAR + PCI_REG_BAR0, 32, baseaddr, internalptr);
		if (PCI_MEM_BAR_TYPE(bartype) == MEMBAR64)
		{
			internalptr = internal_write_pci(segment, bus, device, function, BAR + 1 + PCI_REG_BAR0, 32, highbits, internalptr);
			szbits |= (szhighbits << 32);
		}
		else
			szbits |= ((uint64_t)UINT32_MAX << 32);
		szbits &= (UINT64_MAX - 0xF);
		szbits = ~szbits;
		++szbits;
		*BARSIZE = szbits;
	}
end:
	unmap_config(internalptr);
	return ret;
}


static bool checkFunction(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, pci_scan_callback callback);

static bool checkDevice(uint16_t segment, uint16_t bus, uint16_t device, pci_scan_callback callback) {
	uint8_t function = 0;

	uint16_t vendorID = pci_get_vendor_id(segment, bus, device, function);
	if (vendorID == 0xFFFF) return false;        // Device doesn't exist
	if (checkFunction(segment, bus, device, function, callback))
		return true;
	uint16_t headerType = pci_get_header_type(segment, bus, device, function);
	if ((headerType & 0x80) != 0) {
		/* It is a multi-function device, so check remaining functions */
		for (function = 1; function < 8; function++) {
			if (pci_get_vendor_id(segment, bus, device, function) != 0xFFFF) {
				if (checkFunction(segment, bus, device, function, callback))
					return true;
			}
		}
	}
	return false;
}

static bool checkBus(uint16_t segment, uint16_t bus, pci_scan_callback callback) {
	uint8_t device;

	for (device = 0; device < 32; device++) {
		if (checkDevice(segment, bus, device, callback))
			return true;
	}
	return false;
}

static bool checkFunction(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, pci_scan_callback callback) {
	uint8_t baseClass;
	uint8_t subClass;
	uint8_t secondaryBus;

	if ((pci_get_classcode(segment, bus, device, function) >> 8) == 0x0604) {
		secondaryBus = getSecondaryBus(segment, bus, device, function);
		if (checkBus(segment, secondaryBus, callback))
			return true;
	}
	else
	{
		if (callback(segment, bus, device, function))
			return true;
	}
	return false;
}

bool checkAllBuses(uint16_t segment, uint16_t startbus, uint16_t endbus, pci_scan_callback callback) {
	uint8_t function;
	uint8_t bus;

	if (startbus != 0)
		return checkBus(segment, startbus, callback);

	uint8_t headerType = pci_get_header_type(segment, startbus, 0, 0);
	if ((headerType & 0x80) == 0) {
		/* Single PCI host controller */
		if (checkBus(segment, startbus, callback))
			return true;
	}
	else {
		/* Multiple PCI host controllers */
		for (function = 0; function < 8; function++) {
			if (pci_get_vendor_id(segment, startbus, 0, function) != 0xFFFF) break;
			bus = function;
			if (checkBus(segment, bus, callback))
				return true;
		}
	}
	return false;
}

void pci_bus_scan(pci_scan_callback callback)
{
	if (mcfg)
	{
		ACPI_MCFG_ALLOCATION* allocs = mem_after<ACPI_MCFG_ALLOCATION*>(mcfg);
		for (; raw_diff(allocs, mcfg) < mcfg->Header.Length; ++allocs)
		{
			//kprintf(u"Found PCIe segment %d, start bus %d, end bus %d, base address %x\n", allocs->PciSegment, allocs->StartBusNumber, allocs->EndBusNumber, allocs->Address);
			checkAllBuses(allocs->PciSegment, allocs->StartBusNumber, allocs->EndBusNumber, callback);
		}
	}
	else
	{
		checkAllBuses(0, 0, 255, callback);
	}
}

#define PCI_msix_vctrl_mask	0x0002

uint32_t PciAllocateMsi(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t numintrs, dispatch_interrupt_handler handler, void* param)
{
	//Check device supports MSI
	void* internalptr = nullptr;
	uint64_t status;
	internalptr = internal_read_pci(segment, bus, device, function, PCI_REG_COMSTAT, 32, &status, internalptr);
	status >>= 16;
	if ((status & PCI_STATUS_CAPLIST) != 0)
	{
		uint64_t capptr = 0, capreg = 0, msireg = 0;
		internalptr = internal_read_pci(segment, bus, device, function, PCI_REG_CAPPTR, 32, &capptr, internalptr);
		capptr &= 0xFF;
		capptr /= 4;
		while (capptr != 0)
		{
			internalptr = internal_read_pci(segment, bus, device, function, capptr, 32, &capreg, internalptr);
			if ((capreg & 0xFF) == PCI_CAP_ID_MSIX)
			{
				msireg = capptr;
				//break;
			}
			else if ((capreg & 0xFF) == PCI_CAP_ID_MSI)
			{
				msireg = capptr;
				break;
			}
			capptr = ((capreg >> 8) & 0xFF) / 4;
		}
		if (msireg == 0)	//No MSI support
			goto fail;
		//MSI supported
		//Disable preemption
		auto cpust = arch_disable_interrupts();
		uint32_t vector = arch_allocate_interrupt_vector();
		uint32_t cpu_num = pcpu_data.cpuid;
		kprintf(u"MSI Interrupt Vector: %x (p%d)\n", vector, cpu_num);
		//Setup EOI
		arch_register_interrupt_handler(INTERRUPT_SUBSYSTEM_DISPATCH, vector, cpu_num, handler, param);
		arch_install_interrupt_post_event(INTERRUPT_SUBSYSTEM_DISPATCH, vector, cpu_num, &arch_local_eoi);
		//Now we have all per-CPU stuff sorted
		arch_restore_state(cpust);
		//Get MSI address
		uint64_t msi_data = 0;
		paddr_t msi_addr = arch_msi_address(&msi_data, vector, cpu_num);

		internalptr = internal_read_pci(segment, bus, device, function, msireg, 32, &capreg, internalptr);
		if ((capreg & 0xFF) == PCI_CAP_ID_MSIX)
		{
			kprintf(u"Using MSI-X\n");
			//Find the correct BAR
			uint64_t table_bir = 0, pending_bir=0;
			internalptr = internal_read_pci(segment, bus, device, function, msireg + 1, 32, &table_bir, internalptr);
			internalptr = internal_read_pci(segment, bus, device, function, msireg + 2, 32, &pending_bir, internalptr);
#if DEBUG
			kprintf(u"MSI-X capability\n");
			kprintf(u"  %x\n", capreg);
			kprintf(u"  %x\n", table_bir);
			kprintf(u"  %x\n", pending_bir);
#endif

			uint32_t bar = table_bir & 0x7;
			size_t barsize = 0;
			paddr_t msibar = read_pci_bar(segment, bus, device, function, bar, &barsize);

			size_t offset = table_bir & (UINT32_MAX - 0x7);
			size_t pageoffset = offset & (UINT32_MAX - 0xFFF);
			size_t inoffset = offset & 0xFF8;

			size_t tablecount = (((capreg>>16) & 0x7FF) + 1);
			size_t tablesize = tablecount * 16;		//sizeof(table_entry) == 16
			void* mappedtable = find_free_paging(barsize);
#if DEBUG
			kprintf(u"Mapping BAR%d to %x: address %x, length %x\n", bar, mappedtable, msibar, barsize);
#endif
			if (!paging_map(mappedtable, msibar, barsize, PAGE_ATTRIBUTE_NO_CACHING | PAGE_ATTRIBUTE_WRITABLE))
				kprintf(u"MSI-X MAPPING FAILED\n");
#if DEBUG
			kprintf(u"MSI-X BIR %x, table BAR%d (%x), offset %x, count %d\n", table_bir, bar, msibar, offset, tablecount);
			kprintf(u"Message address: %x, data: %x\n", msi_addr, msi_data);
#endif
			//Write upper address
			internalptr = internal_write_pci(segment, bus, device, function, msireg + 1, 32, msi_addr >> 32, internalptr);
			//kprintf(u"MSI-X desired: %x:%x\n", msi_data, msi_addr);
			volatile uint32_t* msitab = raw_offset<volatile uint32_t*>(mappedtable, offset);
			for (int n = 0; n < tablecount; ++n)
			{
				msitab[0 + 4 * n] = msi_addr & (UINT32_MAX - 0x3);		//DWORD aligned
				msitab[1 + 4 * n] = (msi_addr >> 32);
				msitab[2 + 4 * n] = msi_data;
				msitab[3 + 4 * n] = 0;			//Vector control: unmasked
			}
#if DEBUG
			kprintf(u"MSI-X data mapped %x: %x:%x\n", msitab, msitab[1], msitab[0]);
#endif
			//Enable MSI-X
			capreg &= ~(1 << 30);
			internalptr = internal_write_pci(segment, bus, device, function, msireg, 32, capreg | (1 << 31), internalptr);
		}
		else
		{
			//Normal MSI
			kprintf(u"Using MSI\n");
			uint32_t msgctrl = (capreg >> 16);
			bool maskcap = ((msgctrl & (1 << 8)) != 0);
			bool bits64cap = ((msgctrl & (1 << 7)) != 0);
			uint32_t requestedvecs = (msgctrl >> 1) & 0x7;
			//Write message and data
			internalptr = internal_write_pci(segment, bus, device, function, msireg + 1, 32, msi_addr & UINT32_MAX, internalptr);
			uint32_t data_offset = 2;
			if (bits64cap)
			{
				internalptr = internal_write_pci(segment, bus, device, function, msireg + 2, 32, msi_addr >> 32, internalptr);
				++data_offset;
			}
			internalptr = internal_write_pci(segment, bus, device, function, msireg + data_offset, 32, msi_data, internalptr);
			if(maskcap)
				internalptr = internal_write_pci(segment, bus, device, function, msireg + 4, 32, 0, internalptr);
			//Enable MSI
			msgctrl |= 1;
			capreg = (capreg & UINT16_MAX) | (msgctrl << 16);
			internalptr = internal_write_pci(segment, bus, device, function, msireg, 32, capreg, internalptr);
		}
		unmap_config(internalptr);
		return vector;
	}
fail:
	unmap_config(internalptr);
	return -1;
}

#define PCI_TOKEN_BYVENDOR(vendor, device) \
	((device << 16) | vendor)

#define PCI_TOKEN_BYCLASS(classcode, subclass, progif) \
	((classcode << 16) | (subclass << 8) | progif)

static bool driver_init = false;

bool pci_driver_matcher(uint16_t segment, uint16_t bus, uint16_t device, uint8_t function)
{
	uint32_t vendortoken = pci_get_vendordev(segment, bus, device, function);
	auto it = pci_by_vendor.find(vendortoken);
	if (it != pci_by_vendor.end())
	{
		it->second(segment, bus, device, function);
		return false;
	}
	
	uint32_t classtoken = pci_get_classcode(segment, bus, device, function);
	it = pci_by_class.find(classtoken);
	if (it != pci_by_class.end())
	{
		//Perfect match on classcode
		it->second(segment, bus, device, function);
		return false;
	}
	//Try class:subclass only
	classtoken |= 0xFF;
	it = pci_by_class.find(classtoken);
	if (it != pci_by_class.end())
	{
		//Matched
		it->second(segment, bus, device, function);
		return false;
	}
	//Try class only
	classtoken |= 0xFFFF;
	it = pci_by_class.find(classtoken);
	if (it != pci_by_class.end())
	{
		//Matched
		it->second(segment, bus, device, function);
		return false;
	}
	//As a last resort, try vendor only
	vendortoken |= PCI_TOKEN_BYVENDOR(0, 0xFFFF);
	it = pci_by_vendor.find(vendortoken);
	if (it != pci_by_vendor.end())
	{
		it->second(segment, bus, device, function);
		return false;
	}
	//No driver
	return false;
}

CHAIKRNL_FUNC void register_pci_driver(pci_device_registration* registr)
{
	pci_device_declaration* devids = registr->ids_list;
	for (int n = 0; !(devids[n].vendor == PCI_VENDOR_ANY && devids[n].dev_class == PCI_CLASS_ANY); ++n)
	{
		if (devids[n].vendor != PCI_VENDOR_ANY)
		{
			uint32_t token = PCI_TOKEN_BYVENDOR(devids[n].vendor, devids[n].product);
			pci_by_vendor[token] = registr->callback;
		}
		else if (devids[n].dev_class != PCI_CLASS_ANY)
		{
			uint32_t token = PCI_TOKEN_BYCLASS(devids[n].dev_class, devids[n].subclass, devids[n].progif);
			pci_by_class[token] = registr->callback;
		}
	}
	if (driver_init)
	{
		//TODO
	}
}

void initialize_pci_drivers()
{
	pci_bus_scan(&pci_driver_matcher);
	driver_init = true;
}

