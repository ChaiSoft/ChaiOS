#include "pciexpress.h"
#include <acpi.h>
#include <arch/paging.h>
#include <kstdio.h>

static ACPI_TABLE_MCFG* mcfg = nullptr;

#define LEGACY_CONFIG_ADDRESS 0xCF8
#define LEGACY_CONFIG_DATA 0xCFC

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

bool read_pci_config(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t* result)
{
	if (!mcfg)
	{
		return read_pci_legacy(segment, bus, device, function, reg, width, result);
	}
	paddr_t dev = find_pci_device(segment, bus, device, function);
	if (dev == 0)
		return false;
	void* mapped = find_free_paging(PAGESIZE);
	if (!paging_map(mapped, dev, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING))
	{
		kprintf(u"Error mapping PCI memory: %x, %x\n", mapped, dev);
		return false;
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
	paging_free(mapped, PAGESIZE);
	return written;
}

bool write_pci_config(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t value)
{
	paddr_t dev = find_pci_device(segment, bus, device, function);
	if (dev == 0)
		return false;
	void* mapped = find_free_paging(PAGESIZE);
	if(!paging_map(mapped, dev, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING))
	{
		kprintf(u"Error mapping PCI memory: %x, %x\n", mapped, dev);
		return false;
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
	paging_free(mapped, PAGESIZE);
	return written;
}

void initialize_pci_express()
{
	AcpiGetTable(ACPI_SIG_MCFG, 0, (ACPI_TABLE_HEADER**)&mcfg);
	if (!mcfg)
		return;
}

static uint16_t getVendorID(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, 0, 16, &result);
	return result;
}

static uint8_t getHeaderType(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, 3, 32, &result);
	return (result >> 16) & 0xFF;
}

static uint16_t getSecondaryBus(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, 6, 32, &result);
	return (result >> 8) & 0xFF;
}

static uint8_t getBaseClass(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, 2, 32, &result);
	return (result >> 24) & 0xFF;
}

static uint8_t getSubClass(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function)
{
	uint64_t result;
	read_pci_config(segment, bus, device, function, 2, 32, &result);
	return (result >> 16) & 0xFF;
}

static bool checkFunction(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, pci_scan_callback callback);

static bool checkDevice(uint16_t segment, uint16_t bus, uint16_t device, pci_scan_callback callback) {
	uint8_t function = 0;

	uint16_t vendorID = getVendorID(segment, bus, device, function);
	if (vendorID == 0xFFFF) return false;        // Device doesn't exist
	if (checkFunction(segment, bus, device, function, callback))
		return true;
	uint16_t headerType = getHeaderType(segment, bus, device, function);
	if ((headerType & 0x80) != 0) {
		/* It is a multi-function device, so check remaining functions */
		for (function = 1; function < 8; function++) {
			if (getVendorID(segment, bus, device, function) != 0xFFFF) {
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

	baseClass = getBaseClass(segment, bus, device, function);
	subClass = getSubClass(segment, bus, device, function);
	if ((baseClass == 0x06) && (subClass == 0x04)) {
		secondaryBus = getSecondaryBus(segment, bus, device, function);
		if (checkBus(segment, secondaryBus, callback))
			return true;
	}
	else
	{
		if (function == 0)
		{
			if (callback(segment, bus, device))
				return true;
		}
	}
	return false;
}

bool checkAllBuses(uint16_t segment, uint16_t startbus, uint16_t endbus, pci_scan_callback callback) {
	uint8_t function;
	uint8_t bus;

	if (startbus != 0)
		return checkBus(segment, startbus, callback);

	uint8_t headerType = getHeaderType(segment, startbus, 0, 0);
	if ((headerType & 0x80) == 0) {
		/* Single PCI host controller */
		if (checkBus(segment, startbus, callback))
			return true;
	}
	else {
		/* Multiple PCI host controllers */
		for (function = 0; function < 8; function++) {
			if (getVendorID(segment, startbus, 0, function) != 0xFFFF) break;
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