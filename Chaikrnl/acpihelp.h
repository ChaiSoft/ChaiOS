#ifndef CHAIOS_ACPI_H
#define CHAIOS_ACPI_H

#include <stdheaders.h>

typedef uint64_t (*acpi_system_timer)();

void set_rsdp(void*);
void set_acpi_timer(acpi_system_timer);
void start_acpi_tables();
void startup_acpi();

#endif
