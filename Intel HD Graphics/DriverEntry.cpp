#include <chaikrnl.h>
#include <pciexpress.h>
#include <kstdio.h>
#include <string.h>
#include <endian.h>

pci_device_declaration intelhd_devs[] =
{
	{0x8086, 0x3E98, PCI_CLASS_ANY},
	PCI_DEVICE_END
};

typedef enum {

}intelhd_register_t;

paddr_t pmmngr_allloc_contig(size_t numpages)
{
	if (numpages == 0)
		return 0;
	paddr_t phy_addr = pmmngr_allocate(1);
	for (int i = 1; i < numpages; ++i)
	{
		paddr_t alloc = pmmngr_allocate(1);
		if (alloc != phy_addr + i * PAGESIZE)
		{
			kprintf(u"Contiguity failure: %x -> %x, iteration %d\n", phy_addr, alloc, i);
			pmmngr_free(alloc, 1);
			pmmngr_free(phy_addr, i);
			return 0;
		}
	}
	return phy_addr;
}

static bool intelhd_allocate_hardware_buffer(void** mapped_address, paddr_t& phy_addr, size_t objectlength, size_t count)
{
	size_t numpages = DIV_ROUND_UP(objectlength * count, PAGESIZE);
	phy_addr = pmmngr_allloc_contig(numpages);
	if (!phy_addr)
		return false;
	*mapped_address = find_free_paging(numpages * PAGESIZE);
	if (!paging_map(*mapped_address, phy_addr, numpages * PAGESIZE, PAGE_ATTRIBUTE_NO_CACHING | PAGE_ATTRIBUTE_WRITABLE))
		return false;
	return true;
}

class IntelHdRegisters {
public:
	IntelHdRegisters(void* mappedio)
		:mappedmem(mappedio)
	{

	}

	uint32_t read(intelhd_register_t reg)
	{
		return *raw_offset<volatile uint32_t*>(mappedmem, reg);
	}

	void write(intelhd_register_t reg, uint32_t value)
	{
		*raw_offset<volatile uint32_t*>(mappedmem, reg) = value;
	}

private:
	const void* mappedmem;
};

static uint8_t hdgraphics_interrupt(size_t vector, void* param)
{
	return 0;
}

struct ihd_driver_info {
	void* mapped_registers;
	pci_address address;
	void* mapped_graphicsmem;
};

bool intelhd_init(ihd_driver_info* dinfo)
{
	PciAllocateMsi(dinfo->address.segment, dinfo->address.bus, dinfo->address.device, dinfo->address.function, 1, &hdgraphics_interrupt, dinfo);
	IntelHdRegisters Registers(dinfo->mapped_registers);
	return false;
}


bool IntelHDFinder(uint16_t segment, uint16_t bus, uint16_t device, uint8_t function)
{
	size_t barsize = 0;
	paddr_t devbase = read_pci_bar(segment, bus, device, function, 0, &barsize);
	void* mapped_controller = find_free_paging(barsize);
	if (!paging_map(mapped_controller, devbase, barsize, PAGE_ATTRIBUTE_NO_CACHING | PAGE_ATTRIBUTE_WRITABLE))
	{
		kprintf(u"Error: could not map Intel HD Graphics Registers: size %x\n", barsize);
		return false;
	}

	paddr_t gmembase = read_pci_bar(segment, bus, device, function, 2, &barsize);
	void* mapped_gmem = find_free_paging(barsize);
	if (!paging_map(mapped_gmem, gmembase, barsize, PAGE_ATTRIBUTE_NO_CACHING | PAGE_ATTRIBUTE_WRITABLE))
	{
		kprintf(u"Error: could not map Intel HD Graphics Memory: size %x\n", barsize);
		return false;
	}

	kprintf(u"Found Intel HD Graphics! %d:%d:%d:%d\n", segment, bus, device, function);


	ihd_driver_info* state = new ihd_driver_info;
	state->mapped_registers = mapped_controller;
	state->mapped_graphicsmem = mapped_gmem;
	state->address.bus = bus;
	state->address.device = device;
	state->address.segment = segment;
	state->address.function = function;
	intelhd_init(state);
	return false;
}

static pci_device_registration dev_reg_pci = {
	intelhd_devs,
	& IntelHDFinder
};

EXTERN int CppDriverEntry(void* param)
{
	//Find relevant devices
	register_pci_driver(&dev_reg_pci);
	return 0;
}