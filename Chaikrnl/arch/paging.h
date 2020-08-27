#ifndef CHAIOS_ARCH_PAGING_H
#define CHAIOS_ARCH_PAGING_H

#include <stdheaders.h>
#include <kernelinfo.h>
#include <pmmngr.h>
#include <chaikrnl.h>

typedef void* vaddr_t;

#define PAGE_ATTRIBUTE_WRITABLE 0x2
#define PAGE_ATTRIBUTE_USER 0x4
#define PAGE_ATTRIBUTE_NO_EXECUTE 0x80000
#define PAGE_ATTRIBUTE_NO_PAGING 0x100000
#define PAGE_ATTRIBUTE_NO_CACHING 0x200000
#define PAGE_ATTRIBUTE_WRITE_THROUGH 0x400000

#define PADDR_ALLOCATE UINT64_MAX

EXTERN CHAIKRNL_FUNC bool paging_map(void* vaddr, paddr_t paddr, size_t length, size_t attributes);
EXTERN CHAIKRNL_FUNC void paging_free(void* vaddr, size_t length, bool free_physical = true);
EXTERN CHAIKRNL_FUNC bool check_free(void* vaddr, size_t length);
EXTERN CHAIKRNL_FUNC void set_paging_attributes(void* vaddr, size_t length, size_t attrset, size_t attrclear);
EXTERN CHAIKRNL_FUNC paddr_t get_physical_address(void* addr);

#pragma pack(push, 1)
typedef struct _tag_paddr_buf_desc {
	paddr_t phyaddr;
	size_t length;
}PAGING_PHYADDR_DESC, *PPAGING_PHYADDR_DESC;
#pragma pack (pop)

/*
PagingGetPhysicalAddresses - Returns a list of contiguous physical address pointed to by the virtual address vaddr
length - size of the virtual address range being checked
paddrbuf - Buffer in which to store physical addresses, or NULL
bufsize - Size of paddrbuf, in number of PAGING_PHYADDR_DESCs
userModeRequest - If set, ensures that the virtual address is user mode memory

Return value - Number of descriptors written to paddrbuf, or the number of descriptors required if paddrbuf is NULL
*/
EXTERN CHAIKRNL_FUNC size_t PagingGetPhysicalAddresses(void* __user vaddr, size_t length, PPAGING_PHYADDR_DESC paddrbuf, size_t bufsize, bool userModeRequest, bool lockBuffer);

static inline size_t PagingPrepareDma(void* __user vaddr, size_t length, PPAGING_PHYADDR_DESC paddrbuf, size_t bufsize, bool userModeRequest)
{
	return PagingGetPhysicalAddresses(vaddr, length, paddrbuf, bufsize, userModeRequest, true);
}

EXTERN CHAIKRNL_FUNC void PagingFinishDma(void* __user vaddr, size_t length);

void fill_arch_paging_info(void*& info);
void paging_initialize(void*& info);
void paging_boot_free();

#ifdef X64
#define PAGING_SCRATCH_START ((void*)0xFFFFE00000000000)
#define PAGING_SCRATCH_SEARCH_OFFSET 0x1000000
#else
#error "Unknown architecture"
#endif

static inline void* find_free_paging(size_t sz, void* start = PAGING_SCRATCH_START)
{
	
	while (!check_free(start, sz))
	{
		start = raw_offset<void*>(start, PAGING_SCRATCH_SEARCH_OFFSET);
	}
	return start;
}


#endif
