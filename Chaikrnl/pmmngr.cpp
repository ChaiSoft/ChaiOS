#include "pmmngr.h"
#include <kstdio.h>
#include <acpi.h>
#include <arch/cpu.h>
#include <redblack.h>
#include <linkedlist.h>
#include <string.h>
#include <arch/paging.h>

struct pmmngr_boot_info {
	paddr_t* pgstack;
	paddr_t* pgstackptr;
	paddr_t* alstack;
	paddr_t* alstackptr;
};

static paddr_t* early_stack;
static paddr_t* early_stack_ptr;
static paddr_t* allocated_stack;
static paddr_t* allocated_stack_ptr;

static bool early_mode = true;

static paddr_t max_phy_addr = 0;
static size_t num_colours = 1;
static numa_t numa_domains = 1;

typedef int32_t spage_off_t;

#pragma pack(push, 1)
struct page {
	union {
		//inplace_tree_offset<int32_t> tree_node;
		linked_list_node_off<spage_off_t> list_node;
	};
	union {
		void* pte;
		void** ptes;
	};
	uint32_t ref_count;
	union {
		uint32_t flags;
		struct {
			uint32_t shared : 1;
			uint32_t usable : 1;
			uint32_t reserved : 30;
		}flag;
	};
};
#pragma pack(push, pop)

struct MemoryRegionInfo {
	paddr_t MemoryBase;
	paddr_t MemoryLength;
	numa_t NumaDomain;
	MemoryRegionInfo* next;
};

typedef RedBlackTree<paddr_t, MemoryRegionInfo*> numa_memory_info;
typedef RedBlackTree<numa_t, MemoryRegionInfo*> numa_domain_info;
static numa_memory_info meminf;
static numa_domain_info domaininf;

static MemoryRegionInfo default_info = { 0, UINT64_MAX, 0, nullptr };

static MemoryRegionInfo* architectural_regions = nullptr;

typedef void(*memory_map_callback)(paddr_t start, paddr_t length, bool usable, bool free, void* param);

typedef LinkedListOff<page*, spage_off_t> LinkedListAllocator;

static LinkedListAllocator** regions_allocator[ARCH_PHY_REGIONS_MAX];

static page* PFD = nullptr;
static size_t PFD_ENTRIES = 0;

static cache_colour GetCacheColour(page* page)
{
	return (page - PFD) % num_colours;
}

static cache_colour GetCacheColour(paddr_t paddr)
{
	return (paddr / PAGESIZE) % num_colours;
}

static page* GetPFD(paddr_t paddr)
{
	return &PFD[paddr / PAGESIZE];
}
static paddr_t GetPaddr(page* pg)
{
	return (pg - PFD)*PAGESIZE;
}

static linked_list_node_off<spage_off_t>& get_list_node(page* pg)
{
	return pg->list_node;
}

#ifdef X64
static void* start_search = (void*)0xFFFFD00000000000;
#else
#error "Unknown Architecture"
#endif

static page* create_pfd(size_t entries)
{
	//Simple address space scan
	size_t act_sz = entries * sizeof(page);
	void* PFD_SLOT = find_free_paging(act_sz);
	if (!paging_map(PFD_SLOT, PADDR_ALLOCATE, act_sz, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_EXECUTE))
		return nullptr;
	return (page*)PFD_SLOT;
}

#pragma pack(push, 8)
typedef struct {
	uint32_t Type;
	uint64_t PhysicalStart;
	uint64_t VirtualStart;
	uint64_t NumberOfPages;
	uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;
#pragma pack(pop)

struct EfiMemoryMap {
	EFI_MEMORY_DESCRIPTOR* memmap;
	size_t MemMapSize, MapKey, DescriptorSize;
	uint32_t DescriptorVersion;
};

typedef enum {
	EfiReservedMemoryType,
	EfiLoaderCode,
	EfiLoaderData,
	EfiBootServicesCode,
	EfiBootServicesData,
	EfiRuntimeServicesCode,
	EfiRuntimeServicesData,
	EfiConventionalMemory,
	EfiUnusableMemory,
	EfiACPIReclaimMemory,
	EfiACPIMemoryNVS,
	EfiMemoryMappedIO,
	EfiMemoryMappedIOPortSpace,
	EfiPalCode,
	EfiPersistentMemory,
	EfiMaxMemoryType
} EFI_MEMORY_TYPE;


void initialize_pmmngr(PMMNGR_INFO& info)
{
	pmmngr_boot_info* binf = (pmmngr_boot_info*)info;
	early_stack = binf->pgstack;
	early_stack_ptr = binf->pgstackptr;
	allocated_stack = binf->alstack;
	allocated_stack_ptr = binf->alstackptr;
}

static MemoryRegionInfo* get_memory_region(paddr_t base)
{
	/*
	if (meminf.empty())
		return &default_info;
		*/
	numa_memory_info::iterator it = meminf.near(base);
	while (it->second->MemoryBase > base)
	{
		--it;
		if (it == meminf.begin())
			break;
	}
	return it->second;
}

static void split_on_boundary(paddr_t bound)
{
	numa_memory_info::iterator it = meminf.near(bound);
	while (it->second->MemoryBase > bound)
	{
		--it;
		if (it == meminf.begin())
			break;
	}
	MemoryRegionInfo* inf = it->second;
	if (inf->MemoryBase < bound && inf->MemoryBase + inf->MemoryLength > bound)
	{
		//Make a new region for the split
		MemoryRegionInfo* newregion = new MemoryRegionInfo;
		newregion->MemoryBase = bound;
		newregion->MemoryLength = inf->MemoryBase + inf->MemoryLength - bound;
		newregion->NumaDomain = inf->NumaDomain;
		newregion->next = inf->next;
		//Insert into linked list
		inf->next = newregion;
		//Adjust length of inf
		inf->MemoryLength = bound - inf->MemoryBase;
		//Insert into meminf
		meminf[bound] = newregion;
	}
}

#if defined X86 || defined X64
static void handle_memory_regions()
{
	//Split DMA regions on boundaries
	split_on_boundary(16 * MB);
	split_on_boundary(4ui64 * GB);
}
#endif

static void prepare_numa(ACPI_TABLE_SRAT* srat)
{
	if (!srat)
	{
		meminf[0] = &default_info;
		domaininf[0] = &default_info;
		return;
	}
	ACPI_SUBTABLE_HEADER* subtable = mem_after<ACPI_SUBTABLE_HEADER*>(srat);
	while (raw_diff(subtable, srat) < srat->Header.Length)
	{
		switch (subtable->Type)
		{
		case ACPI_SRAT_TYPE_MEMORY_AFFINITY:
			ACPI_SRAT_MEM_AFFINITY* memaff;
			memaff = reinterpret_cast<ACPI_SRAT_MEM_AFFINITY*>(subtable);
			MemoryRegionInfo* info = new MemoryRegionInfo;
			info->MemoryBase = memaff->BaseAddress;
			info->MemoryLength = memaff->Length;
			info->NumaDomain = memaff->ProximityDomain;
			info->next = nullptr;
			meminf[memaff->BaseAddress] = info;
			numa_domain_info::iterator it = domaininf.find(memaff->ProximityDomain);
			if (it != domaininf.end())
			{
				MemoryRegionInfo* domainchain = it->second;
				while (domainchain->next)
					domainchain = domainchain->next;
				domainchain->next = info;
			}
			else
			{
				domaininf[memaff->ProximityDomain] = info;
			}
			
			break;
		}
		subtable = raw_offset<ACPI_SUBTABLE_HEADER*>(subtable, subtable->Length);
	}
	
}

static bool EfiUsableMemory(EFI_MEMORY_TYPE type)
{
	switch (type)
	{
	case EfiLoaderCode:
		return true;
		break;
	case EfiLoaderData:
		return true;
		break;
	case EfiBootServicesCode:
		return true;
		break;
	case EfiBootServicesData:
		return true;
		break;
	case EfiRuntimeServicesCode:
		return false;
		break;
	case EfiRuntimeServicesData:
		return false;
		break;
	case EfiConventionalMemory:
		return true;
	case EfiUnusableMemory:
		return false;
	case EfiACPIReclaimMemory:
		return true;
	case EfiACPIMemoryNVS:
		return false;
	case EfiMemoryMappedIO:
		return false;
	case EfiMemoryMappedIOPortSpace:
		return false;
	case EfiPalCode:
		return false;
	case EfiPersistentMemory:
		return true;
	case EfiReservedMemoryType:
	default:
		return false;
	}
}

static bool EfiFreeMemory(EFI_MEMORY_TYPE type)
{
	switch (type)
	{
	case EfiLoaderCode:
		return true;
		break;
	case EfiLoaderData:
		return true;
		break;
	case EfiBootServicesCode:
		return true;
		break;
	case EfiBootServicesData:
		return true;
		break;
	case EfiRuntimeServicesCode:
		return false;
		break;
	case EfiRuntimeServicesData:
		return false;
		break;
	case EfiConventionalMemory:
		return true;
	case EfiUnusableMemory:
		return false;
	case EfiACPIReclaimMemory:
		return false;
	case EfiACPIMemoryNVS:
		return false;
	case EfiMemoryMappedIO:
		return false;
	case EfiMemoryMappedIOPortSpace:
		return false;
	case EfiPalCode:
		return false;
	case EfiPersistentMemory:
		return true;
	case EfiReservedMemoryType:
	default:
		return false;
	}
}

static bool EfiUsableMemory(uint32_t type)
{
	return EfiUsableMemory((EFI_MEMORY_TYPE)type);
}

static bool EfiFreeMemory(uint32_t type)
{
	return EfiFreeMemory((EFI_MEMORY_TYPE)type);
}

static size_t gcd(size_t v1, size_t v2)
{
	while (v2 != 0)
	{
		size_t t = v2;
		v2 = v1 % v2;
		v1 = t;
	}
	return v1;
}

static size_t lcm(size_t v1, size_t v2)
{
	return (v1 / gcd(v1, v2))*v2;
}
static void cache_info_callback(uint8_t level, ARCH_CACHE_TYPE type)
{
	size_t associativity = cpu_get_cache_associativity(level, type);
	if (associativity == CACHE_FULLY_ASSOCIATIVE)
		return;
	size_t cachesize = cpu_get_cache_size(level, type);
	size_t colours = cachesize / (associativity*PAGESIZE);
	if (colours == 0)
		return;
	num_colours = lcm(num_colours, colours);
}

static void EfiIterateMemoryMap(EfiMemoryMap* map, memory_map_callback callback, void* param)
{
	EFI_MEMORY_DESCRIPTOR* mem = map->memmap;
	while (raw_diff(mem, map->memmap) < map->MemMapSize)
	{
		uint64_t start = mem->PhysicalStart;
		uint64_t length = mem->NumberOfPages * 4096;
		bool usable = EfiUsableMemory(mem->Type);
		bool free = EfiFreeMemory(mem->Type);
		callback(start, length, usable, free, param);

		mem = raw_offset< EFI_MEMORY_DESCRIPTOR*>(mem, map->DescriptorSize);
	}
}

static void NumaPopulatePmmInfo(paddr_t start, paddr_t length, bool usable, bool free, void* param)
{
	typedef RedBlackTree<numa_t, paddr_t> map_type;
	map_type* ptr_map = (map_type*)param;
	map_type& numa_domain_sizes = *ptr_map;
	uint64_t pages_in_domain = 0;
	if (start + length > max_phy_addr)
		max_phy_addr = start + length;
	while (length > 0) {
		MemoryRegionInfo* rinfo = get_memory_region(start);
		if (rinfo->MemoryBase + rinfo->MemoryLength < start + length)
		{
			//kprintf(u"Memory region spanning NUMA domains: %x, %x\n", start, length * 4096);
			pages_in_domain = (rinfo->MemoryBase + rinfo->MemoryLength - start) / PAGESIZE;
		}
		else
		{
			pages_in_domain = length / PAGESIZE;
		}
		if (usable)
		{
			numa_domain_sizes[rinfo->NumaDomain] += pages_in_domain;
		}
		length -= pages_in_domain * PAGESIZE;
		start += pages_in_domain * PAGESIZE;
	}
}

#if defined X86 || defined X64
static uint8_t GetMemRegion(paddr_t addr)
{
	if (addr < 16 * MB)
		return ARCH_PHY_REGION_ISADMA;
	else if (addr < 4ui64 * GB)
		return ARCH_PHY_REGION_PCIDMA;
	else
		return ARCH_PHY_REGION_NORMAL;
}
#endif

static void CreatePageDatabase()
{
	page* pg = PFD;
	pg->list_node.next = num_colours * sizeof(page);
	pg->list_node.prev = -(num_colours * sizeof(page));
	pg->pte = nullptr;
	pg->ref_count = 0;
	pg->flag.shared = 0;
	pg->flag.usable = 1;
	++pg;
	size_t pass_length = 1;
	while (GetPaddr(pg) + pass_length*PAGESIZE < max_phy_addr)
	{
		for (size_t progess = 0; progess < pass_length / (256 * MB / PAGESIZE); ++progess)
			kprintf(u".");
		memcpy(pg, PFD, sizeof(page)*pass_length);
		pg += pass_length;
		pass_length *= 2;	
	}
	//Finish the fill
	memcpy(pg, PFD, (GetPFD(max_phy_addr) - pg) * sizeof(page));
}

static void FillPageDatabase(paddr_t start, paddr_t length, bool usable, bool free, void* param)
{
	uint64_t pages_in_domain = 0;
	while (length > 0) {
		MemoryRegionInfo* rinfo = get_memory_region(start);
		if (rinfo->MemoryBase + rinfo->MemoryLength < start + length)
		{
			pages_in_domain = (rinfo->MemoryBase + rinfo->MemoryLength - start) / 4096;
		}
		else
		{
			pages_in_domain = length / PAGESIZE;
		}
		uint64_t pages_left = pages_in_domain;
		auto numa_alloc = regions_allocator[GetMemRegion(start)][rinfo->NumaDomain];
		if (free)
		{
			for (size_t n = 0; n < num_colours && n*PAGESIZE < length; ++n)
			{
				paddr_t cur_addr = start + n * PAGESIZE;
				cache_colour colour = GetCacheColour(cur_addr);
				paddr_t colour_end = start + length;
				cache_colour end_colour = GetCacheColour(colour_end);
				if (end_colour > colour)
					colour_end -= (GetCacheColour(colour_end) - colour) * PAGESIZE;
				else
					colour_end -= (num_colours - colour) * PAGESIZE;
				size_t pages_in_list = 1 + (colour_end - cur_addr) / (PAGESIZE*num_colours);
				numa_alloc[colour].join_existing_list(GetPFD(cur_addr), GetPFD(colour_end), pages_in_list);
			}
		}
		else
		{
			paddr_t cur_addr = start;
			while (pages_left > 0)
			{
				page* pg = GetPFD(cur_addr);
				pg->ref_count = 1;
				if (!usable)
					pg->flag.usable = 0;
				cur_addr += PAGESIZE;
				--pages_left;
			}
		}
		//numa_alloc[GetCacheColour(pg)];
		start += pages_in_domain * PAGESIZE;
		length -= pages_in_domain * PAGESIZE;
	}
}

static void uefi_startup(void* memmap)
{
	EfiMemoryMap* map = (EfiMemoryMap*)memmap;
	//NUMA information
	ACPI_TABLE_SRAT* srat = nullptr;
	AcpiGetTable(ACPI_SIG_SRAT, 0, (ACPI_TABLE_HEADER**)&srat);
	prepare_numa(srat);
	//Number of cache colours
	iterate_cpu_caches(&cache_info_callback);
	//Read the UEFI memory map in light of NUMA
	RedBlackTree<numa_t, paddr_t> numa_domain_sizes;

	EfiIterateMemoryMap(map, &NumaPopulatePmmInfo, &numa_domain_sizes);
	handle_memory_regions();
	for (uint_fast8_t region = 0; region < ARCH_PHY_REGIONS_MAX; ++region)
	{
		regions_allocator[region] = new LinkedListAllocator*[domaininf.size()];
		for (RedBlackTree<numa_t, paddr_t>::iterator it = numa_domain_sizes.begin(); it != numa_domain_sizes.end(); ++it)
		{
			regions_allocator[region][it->first] = new LinkedListAllocator[num_colours];
			for (size_t i = 0; i < num_colours; ++i)
				regions_allocator[region][it->first][i].init(&get_list_node);
		}
	}
	numa_domains = domaininf.size();
	kprintf(u"PMMNGR using %d cache colours and %d NUMA domains\n", num_colours, numa_domains);
	//Build page state information
	PFD_ENTRIES = (max_phy_addr / PAGESIZE);
	PFD = create_pfd(PFD_ENTRIES);
	if (!PFD)
		return kprintf(u"Failed to allocate PFD\n");
	kprintf(u"Filling PFD at %x: ", PFD);
	//Build the linked list elements before adding to the linked list
	CreatePageDatabase();
	EfiIterateMemoryMap(map, &FillPageDatabase, nullptr);
	kprintf(u"\n");
	//Now handle the early allocated stack

	while (allocated_stack_ptr != allocated_stack)
	{
		paddr_t alloc_page = *--allocated_stack_ptr;
		page* page = GetPFD(alloc_page);
		++page->ref_count;
		uint8_t region = GetMemRegion(alloc_page);
		numa_t dom = get_memory_region(alloc_page)->NumaDomain;
		cache_colour col = GetCacheColour(alloc_page);
		regions_allocator[region][dom][col].remove(page);
	}
	
	early_mode = false;
}

void startup_pmmngr(BootType mmaptype, void* memmap)
{
	switch (mmaptype)
	{
	case CHAIOS_BOOT_TYPE_UEFI:
		return uefi_startup(memmap);
	}
	kprintf(u"Error: Unsupported boot type %d\n", mmaptype);
}

static numa_t striper = 0;
static cache_colour col_balance = 0;

paddr_t pmmngr_allocate(size_t pages, uint8_t region, numa_t domain, cache_colour colour)
{
	if (early_mode)
	{
		if (pages != 1)
			return 0;
		if (early_stack_ptr == early_stack)
			return 0;
		paddr_t result = *--early_stack_ptr;
		*allocated_stack_ptr++ = result;
		return result;
	}
	else
	{
		if (pages != 1)
			return 0;
		if (domain == NUMA_STRIPE)
		{
			domain = striper++;
			if (striper == numa_domains)
				striper = 0;
		}
		if (colour == CACHE_COLOUR_NONE)
		{
			colour = col_balance++;
			if (col_balance == num_colours)
				col_balance = 0;
		}
		auto region_alloc = regions_allocator[region];
		LinkedListAllocator& ideal_alloc = region_alloc[domain][colour];
		page* val = ideal_alloc.pop();
		if (!val)
		{
			for (numa_t dom = domain; val == nullptr && dom < domain + numa_domains; ++dom)
			{
				numa_t act_dom = dom >= numa_domains ? dom - numa_domains : dom;
				for (cache_colour col = colour + 1; val == nullptr && col < colour + num_colours; ++col)
				{
					cache_colour act_col = col >= num_colours ? col - num_colours : col;
					val = region_alloc[act_dom][act_col].pop();
				}
			}
		}
		++(val->ref_count);
		return val == nullptr ? 0 : GetPaddr(val);
	}
}

void pmmngr_free(paddr_t addr, size_t length)
{
	for (size_t n = 0; n < length; ++n, addr += PAGESIZE)
	{
		page* pg = GetPFD(addr);
		--pg->ref_count;
		if (pg->ref_count == 0 && pg->flag.usable)
		{
			uint8_t region = GetMemRegion(addr);
			numa_t domain = get_memory_region(addr)->NumaDomain;
			cache_colour col = GetCacheColour(addr);
			regions_allocator[region][domain][col].insert(pg);
		}
	}
}
