#include "pciexpress.h"
#include <acpi.h>
#include <arch/paging.h>
#include <kstdio.h>

static ACPI_TABLE_MCFG* mcfg = nullptr;

static paddr_t find_pci_device(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function)
{
	ACPI_MCFG_ALLOCATION* allocs = mem_after<ACPI_MCFG_ALLOCATION*>(mcfg);
	for (; raw_diff(allocs, mcfg) < mcfg->Header.Length; ++allocs)
	{
		if (allocs->PciSegment == segment && allocs->StartBusNumber <= bus && bus <= allocs->EndBusNumber)
			break;
	}
	if (raw_diff(allocs, mcfg) >= mcfg->Header.Length)
		return 0;
	paddr_t paddr_page = allocs->Address + ((bus - allocs->StartBusNumber) << 20) | (device << 15) | (function << 12);
	return paddr_page;
}

bool read_pci_config(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t* result)
{
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