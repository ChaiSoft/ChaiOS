#include "pmmngr.h"
#include "kterm.h"

static paddr_t* pagestack = nullptr;
static paddr_t* stackptr = nullptr;

static paddr_t* allocated_stack = nullptr;
static paddr_t* allocated_ptr = nullptr;

void InitializePmmngr(const EfiMemoryMap& memmap, void* buffer, size_t bufsize)
{
	//Build a physical page stack
	stackptr = pagestack = (paddr_t*)buffer;

	bufsize /= 2;
	allocated_ptr = allocated_stack = raw_offset<paddr_t*>(buffer, bufsize);


	EFI_MEMORY_DESCRIPTOR* current = memmap.memmap;
	while (raw_diff(current, memmap.memmap) < memmap.MemMapSize)
	{
		if (current->Type == EfiConventionalMemory || current->Type == EfiPersistentMemory)
		{
			paddr_t addr = current->PhysicalStart;
			size_t numpages = current->NumberOfPages;
			if (EFI_PAGE_SIZE < PAGESIZE)
			{
				numpages /= (PAGESIZE / EFI_PAGE_SIZE);
			}
			else if (EFI_PAGE_SIZE > PAGESIZE)
			{
				numpages *= (EFI_PAGE_SIZE / PAGESIZE);
			}
			while (numpages > 0 && raw_diff(stackptr, pagestack) < bufsize)
			{
				*stackptr++ = addr;
				--numpages;
				addr += PAGESIZE;
			}
			if (raw_diff(stackptr, pagestack) >= bufsize)
				break;
		}
		current = raw_offset<EFI_MEMORY_DESCRIPTOR*>(current, memmap.DescriptorSize);
	}

}

paddr_t PmmngrAllocate()
{
	if (stackptr == pagestack)
		return 0;
	else
	{
		paddr_t allocated = *--stackptr;
		*allocated_ptr++ = allocated;
		return allocated;
	}
}

void PmmngrFree(paddr_t addr)
{
	*stackptr++ = addr;
}

static struct {
	paddr_t* pgstack;
	paddr_t* pgstackptr;
	paddr_t* alstack;
	paddr_t* alstackptr;
}pmmngr_boot_info;

void fill_pmmngr_info(PMMNGR_INFO& binf)
{
	binf = &pmmngr_boot_info;
	pmmngr_boot_info.alstack = allocated_stack;
	pmmngr_boot_info.alstackptr = allocated_ptr;
	pmmngr_boot_info.pgstack = pagestack;
	pmmngr_boot_info.pgstackptr = stackptr;
}
