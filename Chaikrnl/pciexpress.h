#ifndef CHAIOS_PCI_EXPRESS_H
#define CHAIOS_PCI_EXPRESS_H
#include <stdheaders.h>
#include <arch/paging.h>

struct pci_address {
	uint16_t segment;
	uint16_t bus; 
	uint16_t device; 
	uint16_t function;
};

void initialize_pci_express();
bool read_pci_config(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t* result);
bool write_pci_config(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t value);

paddr_t read_pci_bar(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, size_t BAR, size_t* BARSIZE = nullptr);

uint8_t pci_get_programming_interface(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function);
uint8_t pci_get_base_class(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function);
uint8_t pci_get_subclass(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function);
uint32_t pci_get_classcode(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function);
uint8_t pci_get_header_type(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function);

uint32_t pci_allocate_msi(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t numintrs);

typedef bool(*pci_scan_callback)(uint16_t segment, uint16_t bus, uint16_t device, uint8_t function);
void pci_bus_scan(pci_scan_callback callback);

#endif
