#include <arch/paging.h>
#include <arch/cpu.h>

static void* pml4ptr = 0;
static size_t recursive_slot = 0;

static_assert(sizeof(size_t) == 8, "X64 paging size mismatch");
typedef size_t PML4_ENTRY;
typedef size_t PDPT_ENTRY;
typedef size_t PD_ENTRY;
typedef size_t PTAB_ENTRY;

#define PAGING_PRESENT 0x1
#define PAGING_WRITABLE 0x2
#define PAGING_NOCACHE 0x10
#define PAGING_CHAIOS_NOSWAP 0x200
#define PAGING_SIZEBIT 0x80
#define PAGING_NXE 0x8000000000000000

static void* make_canonical(size_t addr)
{
	if (addr & ((size_t)1 << 47))
		addr |= 0xFFFF000000000000;
	return (void*)addr;
}

static void* make_canonical(void* addr)
{
	return make_canonical((size_t)addr);
}

static size_t decanonical(void* addr)
{
	return (size_t)addr & 0x0000FFFFFFFFFFFF;
}

typedef size_t*(*get_tab_ptr)(void*);
typedef size_t(*get_tab_index)(void*);

static PML4_ENTRY* getPML4(void* addr)
{
	return (PML4_ENTRY*)pml4ptr;
}

static size_t getPML4index(void* addr)
{
	return (decanonical(addr) >> 39) & 0x1FF;
}

static PDPT_ENTRY* getPDPT(void* addr)
{
	return (PDPT_ENTRY*)make_canonical((recursive_slot << 39) | (recursive_slot << 30) | (recursive_slot << 21) | (((decanonical(addr) >> 27) & 0x1FF000)));
}

static size_t getPDPTindex(void* addr)
{
	return (decanonical(addr) >> 30) & 0x1FF;
}

static PD_ENTRY* getPD(void* addr)
{
	return (PD_ENTRY*)make_canonical((recursive_slot << 39) | (recursive_slot << 30) | ((decanonical(addr) >> 18) & 0x3FFFF000));
}

static size_t getPDindex(void* addr)
{
	return (decanonical(addr) >> 21) & 0x1FF;
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

static void clear_ptabs(void* addr)
{
	PTAB_ENTRY* pt = (PTAB_ENTRY*)addr;
	for (size_t n = 0; n < PAGESIZE / sizeof(PTAB_ENTRY); ++n)
	{
		pt[n] = 0;
	}
}

static size_t get_arch_paging_attributes(size_t attributes, bool present = true)
{
	size_t result = 0;
	if (present)
		result |= PAGING_PRESENT;
	if (attributes & PAGE_ATTRIBUTE_WRITABLE)
		result |= PAGING_WRITABLE;
	if (attributes & PAGE_ATTRIBUTE_NO_EXECUTE)
		result |= PAGING_NXE;
	if (attributes & PAGE_ATTRIBUTE_NO_CACHING)
		result |= PAGING_NOCACHE;
	if (attributes & PAGE_ATTRIBUTE_NO_PAGING)
		result |= PAGING_CHAIOS_NOSWAP;
	return result;
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
		paddr_t addr = pmmngr_allocate(1);
		if (addr == 0)
		{
			return false;
		}
		pml4ent = addr | PAGING_PRESENT | PAGING_WRITABLE;
		arch_flush_tlb(pdpt);
		arch_memory_barrier();
		clear_ptabs(pdpt);
	}
	else if (pml4ent & PAGING_SIZEBIT)
		return false;
	PDPT_ENTRY& pdptent = pdpt[getPDPTindex(vaddr)];
	if ((pdptent & PAGING_PRESENT) == 0)
	{
		paddr_t addr = pmmngr_allocate(1);
		if (addr == 0)
		{
			return false;
		}
		pdptent = addr | PAGING_PRESENT | PAGING_WRITABLE;
		arch_flush_tlb(pdir);
		arch_memory_barrier();
		clear_ptabs(pdir);
	}
	else if (pdptent & PAGING_SIZEBIT)
		return false;
	PD_ENTRY& pdent = pdir[getPDindex(vaddr)];
	if ((pdent & PAGING_PRESENT) == 0)
	{
		paddr_t addr = pmmngr_allocate(1);
		if (addr == 0)
		{
			return false;
		}
		pdent = addr | PAGING_PRESENT | PAGING_WRITABLE;
		arch_flush_tlb(ptab);
		arch_memory_barrier();
		clear_ptabs(ptab);
	}
	else if (pdent & PAGING_SIZEBIT)
		return false;
	PTAB_ENTRY& ptabent = ptab[getPTABindex(vaddr)];
	if ((ptabent & PAGING_PRESENT) != 0)
		return false;
	if (paddr == PADDR_ALLOCATE)
	{
		if (!(paddr = pmmngr_allocate(1)))
			return false;
	}
	ptabent = (size_t)paddr | get_arch_paging_attributes(attributes, true);
	arch_flush_tlb(vaddr);
	arch_memory_barrier();
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
	void* endaddr = raw_offset<void*>(vaddr, length - 1);
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
	if (paddr != PADDR_ALLOCATE)
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
		if (paddr != PADDR_ALLOCATE)
			paddr = raw_offset<paddr_t>(paddr, PAGESIZE);
	}
	return true;
}

void set_paging_attributes(void* vaddr, size_t length, size_t attrset, size_t attrclear)
{
	PTAB_ENTRY* ptab = getPTAB(vaddr);
	for (size_t index = getPTABindex(vaddr); index < (length + PAGESIZE - 1) / PAGESIZE; ++index)
	{
		ptab[index] |= get_arch_paging_attributes(attrset);
		ptab[index] &= ~get_arch_paging_attributes(attrclear);
	}
}

struct paging_info  {
	size_t recursive_slot;
	void* pml4ptr;
};

void paging_initialize(void*& info)
{
	paging_info* pinfo = (paging_info*)info;
	recursive_slot = pinfo->recursive_slot;
	pml4ptr = pinfo->pml4ptr;
	//Copy the PML4 so we don't depend on UEFI/bootloader
	
}