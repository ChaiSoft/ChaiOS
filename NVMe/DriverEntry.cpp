#include "NvmeController.h"

#define NVME_PCI_MBAR 0

#define kprintf(...)

bool nvme_finder(uint16_t segment, uint16_t bus, uint16_t device, uint8_t function)
{
	//This is an NVME device
	kprintf(u"NVMe found\n");
	size_t BARSIZE = 0;
	paddr_t pmmio = read_pci_bar(segment, bus, device, function, NVME_PCI_MBAR, &BARSIZE);
	void* mappedmbar = find_free_paging(BARSIZE);
	if (!paging_map(mappedmbar, pmmio, BARSIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_EXECUTE | PAGE_ATTRIBUTE_NO_CACHING))
	{
		kprintf(u"Could not map NVMe MMIO\n");
		return false;
	}
	uint64_t commstat;
	read_pci_config(segment, bus, device, function, 1, 32, &commstat);
	commstat |= (1 << 10);	//Mask pinned interrupts
	commstat |= 0x6;		//Memory space and bus mastering
	write_pci_config(segment, bus, device, function, 1, 32, commstat);

	pci_address addr;
	addr.segment = segment;
	addr.bus = bus;
	addr.device = device;
	addr.function = function;

	NVME* controller = new NVME(mappedmbar, BARSIZE, addr);

	controller->init();
	return false;
}

static pci_device_declaration nvme_devices[] =
{
	{PCI_VENDOR_ANY, PCI_VENDOR_ANY, 0x01, 0x08, 0x02},
	PCI_DEVICE_END
};

static pci_device_registration nvme_pci =
{
	nvme_devices,
	nvme_finder
};

EXTERN int CppDriverEntry(void* param)
{
	//Find relevant devices
	register_pci_driver(&nvme_pci);
	//pci_bus_scan(&nvme_finder);
	return 0;
}