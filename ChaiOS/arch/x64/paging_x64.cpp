#include "arch_paging.h"
#include "assembly.h"
#include "pmmngr.h"
#include "kterm.h"

static void* pml4ptr = 0;
static size_t recursive_slot = 0;

static_assert(sizeof(size_t) == 8, "X64 paging size mismatch");
typedef size_t PML4_ENTRY;
typedef size_t PDPT_ENTRY;
typedef size_t PD_ENTRY;
typedef size_t PTAB_ENTRY;

#define PAGING_PRESENT 0x1
#define PAGING_WRITABLE 0x2

static PML4_ENTRY* getPML4()
{
	if (pml4ptr);
	else
	{
		void* pml4 = get_paging_root();
		pml4ptr = (void*)((size_t)pml4 & ~(size_t)0xFFF);
	}
	return (PML4_ENTRY*)pml4ptr;
}

void arch_initialize_paging()
{
	PML4_ENTRY* pml4 = getPML4();
	//Find a free slot
	for (size_t x = (PAGESIZE / sizeof(PML4_ENTRY));x > 0; --x)
	{
		if ((pml4[x-1] & PAGING_PRESENT) == 0)
		{
			recursive_slot = x-1;
			break;
		}
	}
	printf(u"Paging recursive slot at %d\r\n", recursive_slot);
	pml4[recursive_slot] = (size_t)pml4 | PAGING_PRESENT | PAGING_WRITABLE;
}