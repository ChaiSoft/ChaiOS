#ifndef CHAIOS_PCI_EXPRESS_H
#define CHAIOS_PCI_EXPRESS_H
#include <stdheaders.h>

void initialize_pci_express();
bool read_pci_config(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t* result);
bool write_pci_config(uint16_t segment, uint16_t bus, uint16_t device, uint16_t function, uint32_t reg, uint32_t width, uint64_t value);

#endif
