#include <arch/paging.h>
#include <arch/cpu.h>
#include <kstdio.h>
#include <string.h>
#include <spinlock.h>

static void* pml4ptr = 0;
static size_t recursive_slot = 0;

static_assert(sizeof(size_t) == 8, "X64 paging size mismatch");
typedef size_t PML4_ENTRY;
typedef size_t PDPT_ENTRY;
typedef size_t PD_ENTRY;
typedef size_t PTAB_ENTRY;

#define PAGING_PRESENT 0x1
#define PAGING_WRITABLE 0x2
#define PAGING_USER 0x4
#define PAGING_WRITETHROUGH 0x8
#define PAGING_NOCACHE 0x10
#define PAGING_CHAIOS_NOSWAP 0x200
#define PAGING_SIZEBIT 0x80
#define PAGING_NXE 0x8000000000000000

spinlock_t paging_lock;

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

static void* page_table_address(void* base, size_t index, int level)
{
	return make_canonical(raw_offset<void*>(base, index << (9 * level + 3)));
}

static paddr_t get_paddr(size_t ent)
{
	return ent & (SIZE_MAX - 0xFFF0000000000FFF);
}

static size_t get_attr(size_t ent)
{
	return ent & (SIZE_MAX - 0x000FFFFFFFFFF000);
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

static size_t get_arch_paging_attributes(size_t attributes, bool present = true)
{
	size_t result = 0;
	if (present)
		result |= PAGING_PRESENT;
	if (attributes & PAGE_ATTRIBUTE_WRITABLE)
		result |= PAGING_WRITABLE;
	if (attributes & PAGE_ATTRIBUTE_USER)
		result |= PAGING_USER;
	if (attributes & PAGE_ATTRIBUTE_NO_EXECUTE)
		result |= PAGING_NXE;
	if (attributes & PAGE_ATTRIBUTE_WRITE_THROUGH)
		result |= PAGING_WRITETHROUGH;
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
	bool usersection = getPML4index(vaddr) < 256;
	size_t userflag = usersection ? PAGING_USER : 0;
	if ((pml4ent & PAGING_PRESENT) == 0)
	{
		paddr_t addr = pmmngr_allocate(1);
		if (addr == 0)
		{
			return false;
		}
		pml4ent = addr | PAGING_PRESENT | PAGING_WRITABLE | userflag;
		arch_flush_tlb(pdpt);
		arch_memory_barrier();
		memset(pdpt, 0, PAGESIZE);
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
		pdptent = addr | PAGING_PRESENT | PAGING_WRITABLE | userflag;
		arch_flush_tlb(pdir);
		arch_memory_barrier();
		memset(pdir, 0, PAGESIZE);
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
		pdent = addr | PAGING_PRESENT | PAGING_WRITABLE | userflag;
		arch_flush_tlb(ptab);
		arch_memory_barrier();
		memset(ptab, 0, PAGESIZE);
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
			
		}
		else
		{
			if (!check_free(level - 1, (void*)cur_addr, end_addr))
				return false;
		}
		cur_addr >>= (level * 9 + 3);
		cur_addr += 1;
		cur_addr <<= (level * 9 + 3);
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
	auto st = acquire_spinlock(paging_lock);
	if (!check_free(vaddr, length))
		return false;
	size_t vptr = (size_t)vaddr;
	size_t pgoffset = vptr & (PAGESIZE - 1);
	vaddr = (void*)(vptr ^ pgoffset);
	length += pgoffset;
	if (paddr != PADDR_ALLOCATE)
	{
		if ((paddr & (PAGESIZE - 1)) != pgoffset)
		{
			release_spinlock(paging_lock, st);
			return false;
		}
		paddr ^= pgoffset;
	}

	for (size_t i = 0; i < (length + PAGESIZE - 1) / PAGESIZE; ++i)
	{
		if (!paging_map(vaddr, paddr, attributes))
		{
			release_spinlock(paging_lock, st);
			return false;
		}

		vaddr = raw_offset<void*>(vaddr, PAGESIZE);
		if (paddr != PADDR_ALLOCATE)
			paddr = raw_offset<paddr_t>(paddr, PAGESIZE);
	}
	release_spinlock(paging_lock, st);
	return true;
}

void paging_free(void* vaddr, size_t length, bool free_physical)
{
	PTAB_ENTRY* ptab = getPTAB(vaddr);
	for (size_t index = getPTABindex(vaddr), offset = 0; offset < (length + PAGESIZE - 1) / PAGESIZE; ++index, ++offset)
	{
		paddr_t paddr = ptab[index] & (SIZE_MAX - 0xFFF);
		ptab[index] = 0;
		arch_flush_tlb(raw_offset<void*>(vaddr, index * PAGESIZE));
		if (free_physical)
			pmmngr_free(paddr, length);
	}
}

void set_paging_attributes(void* vaddr, size_t length, size_t attrset, size_t attrclear)
{
	PTAB_ENTRY* ptab = getPTAB(vaddr);
	for (size_t index = getPTABindex(vaddr), count = 0; count < (length + PAGESIZE - 1) / PAGESIZE; ++index, ++count)
	{
		ptab[index] |= get_arch_paging_attributes(attrset);
		ptab[index] &= ~get_arch_paging_attributes(attrclear, false);
	}
	if ((attrset & PAGE_ATTRIBUTE_USER) != 0)
	{
		getPD(vaddr)[getPDindex(vaddr)] |= PAGING_USER;
		getPDPT(vaddr)[getPDPTindex(vaddr)] |= PAGING_USER;
		getPML4(vaddr)[getPML4index(vaddr)] |= PAGING_USER;
	}
	arch_flush_tlb(vaddr);
}

struct paging_info  {
	size_t recursive_slot;
	void* pml4ptr;
};

static paddr_t copy_paging_structure(void* mapping, void* memaddr, int level)
{
	if (level == 0)
		return get_paddr(getPTAB(memaddr)[getPTABindex(memaddr)]);
	size_t* table = get_tab_dispatch[level](memaddr);
	size_t addresses[512];
	for (uint_fast16_t idx = 0; idx < 512; ++idx)
	{
		if (level == 4 && idx < 256)
		{
			addresses[idx] = 0;
			continue;
		}
		if (level == 4 && idx == recursive_slot)
			continue;
		if (table[idx] & PAGING_PRESENT)
		{
			addresses[idx] = copy_paging_structure(mapping, page_table_address(memaddr, idx, level), level - 1);
			addresses[idx] |= get_attr(table[idx]);
		}
		else
			addresses[idx] = 0;
	}
	paddr_t current_tab = pmmngr_allocate(1);
	if (level == 4)
		addresses[recursive_slot] = get_attr(table[recursive_slot]) | current_tab;
	paging_map(mapping, current_tab, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE);
	memcpy(mapping, addresses, PAGESIZE);
	paging_free(mapping, PAGESIZE, false);
	if (level == 5)
	{
		for (size_t n = 0; n < 512; ++n)
		{
			kprintf(u"%x ", ((size_t*)addresses)[n]);
		}
		kprintf(u"Physical %x mapped to %x, %x\n", current_tab, mapping, getPTAB(mapping)[getPTABindex(mapping)]);
		while (1);
	}
	return current_tab;
}

static paddr_t copy_paging_structures()
{
	void* mapping_loc = find_free_paging(PAGESIZE);
	//DFS
	return copy_paging_structure(mapping_loc, nullptr, 4);
}

paddr_t get_physical_address(void* addr)
{
	size_t offset = (size_t)addr & (0xFFF);	
	return get_paddr(getPTAB(addr)[getPTABindex(addr)]) + offset;
}

#define MSR_IA32_PAT 0x277
extern "C" void x64_wrmsr(size_t msr, uint64_t);

void paging_initialize(void*& info)
{
	paging_info* pinfo = (paging_info*)info;
	recursive_slot = pinfo->recursive_slot;
	pml4ptr = pinfo->pml4ptr;
	//Set up PAT
	uint64_t patvalue = 0x0007040600070406;
	x64_wrmsr(MSR_IA32_PAT, patvalue);

	paging_lock = create_spinlock();
}

void paging_boot_free()
{
	//Free anything in lower half. Note that correct physical behaviour is determined by memory map
	//Copy paging structures
	paddr_t new_paging = copy_paging_structures();
	arch_set_paging_root(new_paging);
}