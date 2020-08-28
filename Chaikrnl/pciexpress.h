#ifndef CHAIOS_PCI_EXPRESS_H
#define CHAIOS_PCI_EXPRESS_H
#include <stdheaders.h>
#include <arch/paging.h>
#include <arch/cpu.h>

#pragma pack(push, 1)
struct pci_address {
	uint16_t segment;
	uint16_t bus; 
	uint16_t device; 
	uint16_t function;
};

#define PCI_VENDOR_ANY 0xFFFF
#define PCI_CLASS_ANY 0xFF
struct pci_device_declaration {
	uint16_t vendor;
	uint16_t product;
	uint8_t dev_class;
	uint8_t subclass;
	uint8_t progif;
};

#define PCI_DEVICE_END {PCI_VENDOR_ANY, PCI_VENDOR_ANY, PCI_CLASS_ANY, PCI_CLASS_ANY, PCI_CLASS_ANY}

typedef bool(*pci_scan_callback)(uint16_t segment, uint16_t bus, uint16_t device, uint8_t function);
typedef pci_scan_callback pci_register_callback;

struct pci_device_registration {
	pci_device_declaration* ids_list;
	pci_register_callback callback;
};
#pragma pack(pop)

void initialize_pci_express();
CHAIKRNL_FUNC bool read_pci_config(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t* result);
CHAIKRNL_FUNC bool write_pci_config(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t value);

CHAIKRNL_FUNC paddr_t read_pci_bar(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, size_t BAR, size_t* BARSIZE = nullptr);

CHAIKRNL_FUNC uint16_t pci_get_vendor_id(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function);
CHAIKRNL_FUNC uint16_t pci_get_device_id(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function);
CHAIKRNL_FUNC uint8_t pci_get_programming_interface(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function);
CHAIKRNL_FUNC uint8_t pci_get_base_class(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function);
CHAIKRNL_FUNC uint8_t pci_get_subclass(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function);
CHAIKRNL_FUNC uint32_t pci_get_classcode(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function);
CHAIKRNL_FUNC uint8_t pci_get_header_type(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function);

CHAIKRNL_FUNC uint32_t PciAllocateMsi(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t numintrs, dispatch_interrupt_handler handler, void* param);
CHAIKRNL_FUNC void pci_bus_scan(pci_scan_callback callback);

CHAIKRNL_FUNC void register_pci_driver(pci_device_registration* registr);
void initialize_pci_drivers();

#endif
