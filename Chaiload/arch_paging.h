#ifndef CHAIOS_BOOT_ARCH_PAGING_H
#define CHAIOS_BOOT_ARCH_PAGING_H

#include "pmmngr.h"

#define PAGE_ATTRIBUTE_WRITABLE 0x2
#define PAGE_ATTRIBUTE_NO_EXECUTE 0x80000
#define PAGE_ATTRIBUTE_NO_PAGING 0x100000
#define PAGE_ATTRIBUTE_NO_CACHING 0x200000

void arch_initialize_paging();
bool paging_map(void* vaddr, paddr_t paddr, size_t length, size_t attributes);		//No need for special caching code, we're only an OS loader
bool check_free(void* vaddr, size_t length);
void set_paging_attributes(void* vaddr, size_t length, size_t attrset, size_t attrclear);
void fill_arch_paging_info(void*& info);

#endif
