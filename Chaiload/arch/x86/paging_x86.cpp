#include "arch_paging.h"
#include "assembly.h"
#include "pmmngr.h"
#include "kterm.h"

static void* pdptr = 0;
static size_t recursive_slot = 0;

static_assert(sizeof(size_t) == 4, "X86 paging size mismatch");
typedef size_t PD_ENTRY;
typedef size_t PTAB_ENTRY;

#define PAGING_PRESENT 0x1
#define PAGING_WRITABLE 0x2
#define PAGING_SIZEBIT 0x80

typedef size_t*(*get_tab_ptr)(void*);
typedef size_t(*get_tab_index)(void*);

static PD_ENTRY* getPDIR(void* addr)
{
	if (pdptr);
	else
	{
		void* pd = get_paging_root();
		pdptr = (void*)((size_t)pd & ~(size_t)0xFFF);
	}
	return (PD_ENTRY*)pdptr;
}

static size_t getPDIRindex(void* addr)
{
	return (addr >> 21) & 0x3FF;
}

static PTAB_ENTRY* getPTAB(void* addr)
{
	return (PD_ENTRY*)make_canonical((recursive_slot << 39) | ((decanonical(addr) >> 9) & 0x7FFFFFF000));
}

static size_t getPTABindex(void* addr)
{
	return (decanonical(addr) >> 12) & 0x1FF;
}

static get_tab_ptr get_tab_dispatch[] =
{
	nullptr,
	&getPTAB,
	&getPD,
	&getPDPT,
	&getPML4
};

static get_tab_index get_index_dispatch[] =
{
	nullptr,
	&getPTABindex,
	&getPDindex,
	&getPDPTindex,
	&getPML4index
};

static PML4_ENTRY* setPML4_recursive(PML4_ENTRY* pml4, size_t slot)
{
	recursive_slot = slot;
	pml4[recursive_slot] = (size_t)pml4 | PAGING_PRESENT | PAGING_WRITABLE;
	//Now calculate new PML4 address
	void* new_pml4 = make_canonical((recursive_slot << 39) | (recursive_slot << 30) | (recursive_slot << 21) | (recursive_slot << 12));
	//Flush the TLB
	set_paging_root(get_paging_root());
	pml4ptr = new_pml4;
	return (PML4_ENTRY*)new_pml4;
}

void arch_initialize_paging()
{
	PML4_ENTRY* pml4 = getPML4(nullptr);
	//Find a free slot
	for (size_t x = (PAGESIZE / sizeof(PML4_ENTRY));x > 0; --x)
	{
		if ((pml4[x-1] & PAGING_PRESENT) == 0)
		{
			pml4 = setPML4_recursive(pml4, x-1);
			break;
		}
	}
	printf(u"Paging recursive slot at %d, PML4 @ %x\r\n", recursive_slot, pml4);
}

static void clear_ptabs(void* addr)
{
	PTAB_ENTRY* pt = (PTAB_ENTRY*)addr;
	for (size_t n = 0; n < PAGESIZE / sizeof(PTAB_ENTRY); ++n)
	{
		pt[n] = 0;
	}
}

bool paging_map(void* vaddr, paddr_t paddr, size_t attributes)
{
	PML4_ENTRY* pml4 = getPML4(vaddr);
	PDPT_ENTRY* pdpt = getPDPT(vaddr);
	PD_ENTRY* pdir = getPD(vaddr);
	PTAB_ENTRY* ptab = getPTAB(vaddr);

	PML4_ENTRY& pml4ent = pml4[getPML4index(vaddr)];
	if ((pml4ent & PAGING_PRESENT) == 0)
	{
		paddr_t addr = PmmngrAllocate();
		if (addr == 0)
		{
			return false;
		}
		pml4ent = addr | PAGING_PRESENT | PAGING_WRITABLE;
		tlbflush(pdpt);
		memory_barrier();
		clear_ptabs(pdpt);
	}
	else if (pml4ent & PAGING_SIZEBIT)
		return false;
	PDPT_ENTRY& pdptent = pdpt[getPDPTindex(vaddr)];
	if ((pdptent & PAGING_PRESENT) == 0)
	{
		paddr_t addr = PmmngrAllocate();
		if (addr == 0)
		{
			return false;
		}
		pdptent = addr | PAGING_PRESENT | PAGING_WRITABLE;
		tlbflush(pdir);
		memory_barrier();
		clear_ptabs(pdir);
	}
	else if (pdptent & PAGING_SIZEBIT)
		return false;
	PD_ENTRY& pdent = pdir[getPDindex(vaddr)];
	if ((pdent & PAGING_PRESENT) == 0)
	{
		paddr_t addr = PmmngrAllocate();
		if (addr == 0)
		{
			return false;
		}
		pdent = addr | PAGING_PRESENT | PAGING_WRITABLE;
		tlbflush(ptab);
		memory_barrier();
		clear_ptabs(ptab);
	}
	else if (pdent & PAGING_SIZEBIT)
		return false;
	PTAB_ENTRY& ptabent = ptab[getPTABindex(vaddr)];
	if ((ptabent & PAGING_PRESENT) != 0)
		return false;
	if (paddr == PADDR_T_MAX)
	{
		if (!(paddr = PmmngrAllocate()))
			return false;
	}
	ptabent = (size_t)paddr | PAGING_PRESENT;
	if (attributes & PAGE_ATTRIBUTE_WRITABLE)
		ptabent |= PAGING_WRITABLE;
	tlbflush(vaddr);
	memory_barrier();
	return true;
}

static bool check_free(int level, void* start_addr, void* end_addr)
{
	if (level == 0)
		return false;
	size_t* paging_entry = get_tab_dispatch[level](start_addr);
	get_tab_index getindex = get_index_dispatch[level];

	size_t cur_addr = (size_t)start_addr;
	size_t end_index = getindex(end_addr);
	if (get_tab_dispatch[level](end_addr) != get_tab_dispatch[level](start_addr))
		end_index = 0x1FF;
	for (size_t pindex = getindex(start_addr); pindex <= end_index; ++pindex)
	{
		if ((paging_entry[pindex] & PAGING_PRESENT) == 0)
		{
			cur_addr >>= (level * 9 + 3);
			cur_addr += 1;
			cur_addr <<= (level * 9 + 3);
			continue;
		}
		else
		{
			if (!check_free(level - 1, (void*)cur_addr, end_addr))
				return false;
		}
	}
	return true;
}

bool check_free(void* vaddr, size_t length)
{
	void* endaddr = raw_offset<void*>(vaddr, length-1);
	return check_free(4, vaddr, endaddr);
}

bool paging_map(void* vaddr, paddr_t paddr, size_t length, size_t attributes)
{
	if (!check_free(vaddr, length))
		return false;
	size_t vptr = (size_t)vaddr;
	size_t pgoffset = vptr & (PAGESIZE - 1);
	vaddr = (void*)(vptr ^ pgoffset);
	length += pgoffset;
	if (paddr != PADDR_T_MAX)
	{
		if ((paddr & (PAGESIZE - 1)) != pgoffset)
			return false;
		paddr ^= pgoffset;
	}

	for (size_t i = 0; i < (length + PAGESIZE - 1) / PAGESIZE; ++i)
	{
		if (!paging_map(vaddr, paddr, attributes))
			return false;

		vaddr = raw_offset<void*>(vaddr, PAGESIZE);
		if(paddr != PADDR_T_MAX)
			paddr = raw_offset<paddr_t>(paddr, PAGESIZE);
	}
	return true;
}

void set_paging_attributes(void* vaddr, size_t length, size_t attrset, size_t attrclear)
{
	PTAB_ENTRY* ptab = getPTAB(vaddr);
	for (size_t index = getPTABindex(vaddr); index < (length + PAGESIZE - 1) / PAGESIZE; ++index)
	{
		if ((attrset & PAGE_ATTRIBUTE_WRITABLE) != 0)
		{
			ptab[index] |= PAGING_WRITABLE;
		}

		if ((attrclear & PAGE_ATTRIBUTE_WRITABLE) != 0)
		{
			ptab[index] &= ~((size_t)PAGING_WRITABLE);
		}
	}
}

static struct {
	size_t recursive_slot;
	void* pml4ptr;
}paging_info;

void fill_arch_paging_info(void*& info)
{
	info = &paging_info;
	paging_info.recursive_slot = recursive_slot;
	paging_info.pml4ptr = pml4ptr;
}