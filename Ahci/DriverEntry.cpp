#include <chaikrnl.h>
#include <pciexpress.h>
#include <kstdio.h>
#include <string.h>
#include <endian.h>


bool ahci_finder(uint16_t segment, uint16_t bus, uint16_t device, uint8_t function)
{
	uint32_t classcode = pci_get_classcode(segment, bus, device, function);
	if (classcode!= 0x010601)		//Not an AHCI
		return false;
	//This is an AHCI device

	return false;
}

int DriverEntry(void* param)
{
	//Find relevant devices
	pci_bus_scan(&ahci_finder);
	return 0;
}