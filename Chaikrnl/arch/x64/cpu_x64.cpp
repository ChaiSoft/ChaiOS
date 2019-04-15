#include <arch/cpu.h>
#include <kstdio.h>

extern "C" size_t x64_read_cr0();
extern "C" size_t x64_read_cr2();
extern "C" size_t x64_read_cr3();
extern "C" size_t x64_read_cr4();
extern "C" uint64_t x64_read_xcr0();
extern "C" void x64_write_cr0(size_t);
extern "C" void x64_write_cr3(size_t);
extern "C" void x64_write_cr4(size_t);
extern "C" void x64_write_xcr0(uint64_t);

extern "C" uint64_t x64_rdmsr(size_t msr);
extern "C" void x64_wrmsr(size_t msr, uint64_t);

extern "C" void x64_cpuid(size_t page, size_t* a, size_t* b, size_t* c, size_t* d, size_t subpage = 0);

extern "C" void x64_xsave(size_t* location);
extern "C" void x64_fxsave(size_t* location);
extern "C" void x64_xrstor(size_t* location);
extern "C" void x64_fxrstor(size_t* location);

extern "C" void x64_lgdt(void* location);
extern "C" void x64_sgdt(void* location);

extern "C" void (*x64_save_fpu)(size_t* location) = nullptr;
extern "C" void(*x64_restore_fpu)(size_t* location) = nullptr;

extern "C" uint16_t x64_get_segment_register(size_t reg);
extern "C" void x64_set_segment_register(size_t reg, uint16_t val);

extern "C" uint8_t x64_inportb(uint16_t port);
extern "C" uint16_t x64_inportw(uint16_t port);
extern "C" uint32_t x64_inportd(uint16_t port);

extern "C" void x64_outportb(uint16_t port, uint8_t);
extern "C" void x64_outportw(uint16_t port, uint16_t);
extern "C" void x64_outportd(uint16_t port, uint32_t);

extern "C" int x64_locked_cas(volatile size_t* loc, size_t oldv, size_t newv);

extern "C" cpu_status_t x64_disable_interrupts();
extern "C" void x64_restore_flags(cpu_status_t);
extern "C" void x64_pause();
extern "C" void x64_invlpg(void*);
extern "C" void x64_mfence();
extern "C" void x64_cacheflush();

enum sregs {
	SREG_CS,
	SREG_DS,
	SREG_ES,
	SREG_FS,
	SREG_GS,
	SREG_SS
};

extern "C" size_t xmask = 0;
extern "C" size_t xsavearea_size = 0;

#define IA32_EFER 0xC0000080

static size_t cpuid_max_page = 0;
static size_t max_cpuid_page()
{
	if (cpuid_max_page == 0)
	{
		size_t a, b, c, d;
		x64_cpuid(0, &a, &b, &c, &d, 0);
		cpuid_max_page = a;
	}
	return cpuid_max_page;
}

static size_t cpuid_max_extenxded_page = 0;
static size_t max_cpuid_page_extended()
{
	if (cpuid_max_extenxded_page == 0)
	{
		size_t a, b, c, d;
		x64_cpuid(0x80000000, &a, &b, &c, &d, 0);
		cpuid_max_extenxded_page = a;
	}
	return cpuid_max_extenxded_page;
}

#pragma pack(push, 1)

typedef struct _gdt {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_mid;
	uint8_t access;
	uint8_t flags_limit;
	uint8_t base_high;
}gdt_entry, *pgdt_entry;

typedef struct _gdtr {
	uint16_t size;
	pgdt_entry gdtaddr;
}gdtr, *pgdtr;

#pragma pack(pop)

//SYSCALL requirements:
//IA32_LSTAR contains entry point
//IA32_CSTAR contains RIP for 32 bit programs
//IA32_FMASK is used to mask RFLAGS
//IA32_STAR contains SYSRET_CS

#define GDT_ENTRY_NULL 0
#define GDT_ENTRY_KERNEL_CODE 1
#define GDT_ENTRY_KERNEL_DATA 2
#define GDT_ENTRY_USER_CODE32 3
#define GDT_ENTRY_USER_DATA 4
#define GDT_ENTRY_USER_CODE 5
#define GDT_ENTRY_KERNEL_CODE32 6
#define GDT_ENTRY_TSS 7
#define GDT_ENTRY_MAX 9		//TSS takes two entries

#define SEGVAL(gdtent, rpl) \
	((gdtent << 3) | rpl)

#define SEGVAL_LDT(ldtent, rpl) \
	((ldtent << 3) | 0x4 | rpl)

static const size_t GDT_ENTRIES = GDT_ENTRY_MAX;
__declspec(align(4)) static gdt_entry gdt[GDT_ENTRIES];
__declspec(align(4)) static gdtr the_gdtr, old_gdtr;

static uint16_t oldsregs[8];

#define GDT_FLAG_GRAN 0x8
#define GDT_FLAG_32BT 0x4
#define GDT_FLAG_64BT 0x2

#define GDT_ACCESS_PRESENT 0x80
#define GDT_ACCESS_PRIVL(x) (x << 5)
#define GDT_ACCESS_TYPE 0x10
#define GDT_ACCESS_EX 0x8
#define GDT_ACCESS_DC 0x4
#define GDT_ACCESS_RW 0x2
#define GDT_ACCESS_AC 0x1

static void set_gdt_entry(gdt_entry& entry, size_t base, size_t limit, uint8_t access, uint8_t flags)
{
	entry.base_low = base & 0xFFFF;
	entry.base_mid = (base >> 16) & 0xFF;
	entry.base_high = (base >> 24) & 0xFF;
	entry.limit_low = limit & 0xFFFF;
	entry.access = access;
	entry.flags_limit = (flags << 4) | ((limit >> 16) & 0xF);
}

static void set_gdt_entry(gdt_entry& entry, uint8_t access, uint8_t flags)
{
	access |= GDT_ACCESS_PRESENT | GDT_ACCESS_TYPE;
	flags |= GDT_FLAG_GRAN;
	set_gdt_entry(entry, 0, SIZE_MAX, access, flags);
}

static void fill_gdt(gdt_entry* thegdt)
{
	set_gdt_entry(thegdt[GDT_ENTRY_NULL], 0, 0, 0, 0);
	//Kernel Code segment: STAR.SYSCALL_CS
	set_gdt_entry(thegdt[GDT_ENTRY_KERNEL_CODE], GDT_ACCESS_PRIVL(0) | GDT_ACCESS_RW | GDT_ACCESS_EX, GDT_FLAG_64BT);
	//Kernel Data segment
	set_gdt_entry(thegdt[GDT_ENTRY_KERNEL_DATA], GDT_ACCESS_PRIVL(0) | GDT_ACCESS_RW, GDT_FLAG_32BT);
	//User Code segment (32 bit): STAR.SYSRET_CS
	set_gdt_entry(thegdt[GDT_ENTRY_USER_CODE32], GDT_ACCESS_PRIVL(3) | GDT_ACCESS_RW | GDT_ACCESS_EX, GDT_FLAG_32BT);
	//User Data segment
	set_gdt_entry(thegdt[GDT_ENTRY_USER_DATA], GDT_ACCESS_PRIVL(3) | GDT_ACCESS_RW, GDT_FLAG_32BT);
	//User Code segment (64 bit)
	set_gdt_entry(thegdt[GDT_ENTRY_USER_CODE], GDT_ACCESS_PRIVL(3) | GDT_ACCESS_RW | GDT_ACCESS_EX, GDT_FLAG_64BT);
	//Kernel Code segment (32 bit)
	set_gdt_entry(thegdt[GDT_ENTRY_KERNEL_CODE32], GDT_ACCESS_PRIVL(0) | GDT_ACCESS_RW | GDT_ACCESS_EX, GDT_FLAG_32BT);
}

static void save_sregs()
{
	for (uint_fast8_t reg = 0; reg < 8; ++reg)
		oldsregs[reg] = x64_get_segment_register(reg);
}

static void load_default_sregs()
{
	x64_set_segment_register(SREG_CS, SEGVAL(GDT_ENTRY_KERNEL_CODE, 0));		
	x64_set_segment_register(SREG_DS, SEGVAL(GDT_ENTRY_KERNEL_DATA, 0));
	x64_set_segment_register(SREG_ES, SEGVAL(GDT_ENTRY_KERNEL_DATA, 0));
	x64_set_segment_register(SREG_SS, SEGVAL(GDT_ENTRY_KERNEL_DATA, 0));
	//Per CPU data
	x64_set_segment_register(SREG_FS, SEGVAL(GDT_ENTRY_KERNEL_DATA, 0));
	x64_set_segment_register(SREG_GS, SEGVAL(GDT_ENTRY_KERNEL_DATA, 0));
}

extern "C" void arch_cpu_init()
{
	size_t cr0 = x64_read_cr0();
	//Enable FPU
	cr0 |= (1 << 1) | (1<< 5);
	cr0 &= (SIZE_MAX - (1 << 2) - (1 << 3) - (1<<30) - (1<<29));		//1<<30 turns off cache disable
	cr0 |= (1 << 16);		//Can't write read-only pages in ring 0
	x64_write_cr0(cr0);
	//GDT
	x64_sgdt(&old_gdtr);	//Save old GDT
	save_sregs();			//Save old segment registers
	fill_gdt(gdt);
	the_gdtr.gdtaddr = gdt;
	the_gdtr.size = GDT_ENTRIES * sizeof(gdt_entry) - 1;
	x64_lgdt(&the_gdtr);	//Load new GDT
	//Reload segment registers for the new GDT
	load_default_sregs();
	//Enable SSE if supported
	size_t a, b, c, d;
	size_t max_cpuid = max_cpuid_page();
	if (max_cpuid < 1)
		return;
	x64_cpuid(1, &a, &b, &c, &d, 0);
	//SSE check
	if ((d & (1 << 25)) != 0)
	{
		size_t cr4 = x64_read_cr4();
		cr4 |= (1 << 10);		//OSXMMEXCPT
		//SSE supported. Check for fxsave or xsave
		if ((d & (1 << 24)) != 0)
		{
			//FXSAVE support
			cr4 |= (1 << 9);
			x64_save_fpu = &x64_fxsave;
			xsavearea_size = 512;
		}
		if ((c & (1 << 26)) != 0)
		{
			//XSAVE support
			cr4 |= (1 << 18);
			x64_write_cr4(cr4);		//need to enable XSAVE before using XGETBV
			size_t xcr0 = x64_read_xcr0();
			xcr0 |= 3;
			//Check for AVX
			if ((c & (1 << 28)) != 0)
				xcr0 |= 7;		//AVX enable
			//Check for AVX-512
			if (max_cpuid >= 0xD)
			{
				size_t ax, bx, cx, dx;
				x64_cpuid(0xD, &ax, &bx, &cx, &dx, 0);
				xcr0 |= (ax&(7<<5));		//All supported bits of AVX 512 state
				xsavearea_size = bx;		//Maximum xsave area size
			}
			else
			{
				xsavearea_size = ((xcr0 & 4) != 0) ? 576 : 1088;
			}
			x64_save_fpu = &x64_xsave;
			x64_write_xcr0(xcr0);
			xmask = xcr0;
		}
		x64_write_cr4(cr4);
	}
	//SMEP and SMAP
	if (max_cpuid >= 7)
	{
		x64_cpuid(7, &a, &b, &c, &d, 0);
		size_t cr4 = x64_read_cr4();
		if ((b & (1 << 7)) != 0)
		{
			//SMEP supported
			cr4 |= (1 << 20);
		}
		if ((b & (1 << 20)) != 0)
		{
			//SMAP supported
			cr4 |= (1 << 21);
		}
		x64_write_cr4(cr4);
	}
	//EFER stuff
	size_t efer = x64_rdmsr(IA32_EFER);
	efer |= (1 << 11);		//NXE
	efer |= 1;				//SCE
	x64_wrmsr(IA32_EFER, efer);
}

size_t arch_read_port(size_t port, uint8_t width)
{
	switch (width)
	{
	case 8:
		return x64_inportb(port);
	case 16:
		return x64_inportw(port);
	case 32:
		return x64_inportd(port);
	default:
		return 0;
	}
}
void arch_write_port(size_t port, size_t value, uint8_t width)
{
	switch (width)
	{
	case 8:
		return x64_outportb(port, value);
	case 16:
		return x64_outportw(port, value);
	case 32:
		return x64_outportd(port, value);
	}
}

bool arch_cas(volatile size_t* loc, size_t oldv, size_t newv)
{
	if (x64_locked_cas(loc, oldv, newv) != 0)
		return true;
	else
		return false;
}

typedef size_t cpu_status_t;

cpu_status_t arch_disable_interrupts()
{
	return x64_disable_interrupts();
}
void arch_restore_state(cpu_status_t val)
{
	x64_restore_flags(val);
}

void arch_pause()
{
	x64_pause();
}

void arch_flush_tlb(void* loc)
{
	x64_invlpg(loc);
}
void arch_memory_barrier()
{
	x64_mfence();
}

CHAIKRNL_FUNC void arch_flush_cache()
{
	x64_cacheflush();
}


static int strcmp(const char* s1, const char* s2)
{
	while (*s1 && *s1++ == *s2++);
	return *s1 - *s2;
}

enum CpuVendor {
	VENDOR_UNSET,
	VENDOR_INTEL,
	VENDOR_AMD,
	VENDOR_UNKNOWN
};

static CpuVendor the_vendor = VENDOR_UNSET;
static CpuVendor getCpuVendor()
{
	if (the_vendor != VENDOR_UNSET)
		return the_vendor;
	size_t maxcpuid, b, c, d;
	x64_cpuid(0, &maxcpuid, &b, &c, &d);
	char vendor[13];
	*(uint32_t*)&vendor[0] = b;
	*(uint32_t*)&vendor[4] = d;
	*(uint32_t*)&vendor[8] = c;
	vendor[12] = 0;
	if (strcmp(vendor, "GenuineIntel") == 0)
		return (the_vendor = VENDOR_INTEL);
	else if (strcmp(vendor, "AuthenticAMD") == 0)
		return (the_vendor = VENDOR_AMD);
	return (the_vendor = VENDOR_UNKNOWN);
}

enum wanted_info {
	wanted_linesize,
	wanted_cachesize,
	wanted_associativity,
	wanted_iterate
};
static size_t read_nice_pages(uint8_t cache_level, ARCH_CACHE_TYPE type, CpuVendor vendor, wanted_info info, cpu_cache_callback callback)
{
	size_t cpuid_page = (vendor == VENDOR_INTEL ? 4 : 0x8000001D);
	for (size_t n = 0;; ++n)
	{
		size_t a, b, c, d;
		x64_cpuid(cpuid_page, &a, &b, &c, &d, n);
		size_t cache_type = a & 0x1f;
		if (cache_type == 0)
			break;
		size_t cachelevel = (a >> 5) & 0x7;
		if (cachelevel != cache_level && info != wanted_iterate)
			continue;
		switch (type)
		{
		case CACHE_TYPE_DATA:
			if (cache_type != 1)
				continue;
			break;
		case CACHE_TYPE_INSTRUCTION:
			if (cache_type != 2)
				continue;
			break;
		case CACHE_TYPE_UNIFIED:
			if (cache_type != 3)
				continue;
			break;
		default:
			if (info == wanted_iterate)
			{
				ARCH_CACHE_TYPE type = CACHE_TYPE_UNKNOWN;
				if (cache_type == 1)
					type = CACHE_TYPE_DATA;
				else if (cache_type == 2)
					type = CACHE_TYPE_INSTRUCTION;
				else if (cache_type == 3)
					type = CACHE_TYPE_UNIFIED;
				callback(cachelevel, type);
			}
			else
				return 0;
			break;
		}
		//We found the correct cache
		size_t linesize = (b & 0xFFF) + 1;
		size_t partitions = ((b >> 12) & 0x3FF) + 1;
		size_t associativity = ((b >> 22) & 0x3FF) + 1;
		bool fullyassociative = false;
		if ((a & (1 << 9)) != 0)
			fullyassociative = true;
		size_t sets = c + 1;
		switch (info)
		{
		case wanted_cachesize:
			return linesize * partitions * associativity * sets;
		case wanted_linesize:
			return linesize;
		case wanted_associativity:
			return fullyassociative ? CACHE_FULLY_ASSOCIATIVE : associativity;
		}
	}
	return 0;
}

static const size_t amd_80000006_assoc_table[] = {
	0, 1, 2, 0,
	4, 0, 8, 0,
	16,0,32,48,
	64, 96, 128, CACHE_FULLY_ASSOCIATIVE
};

static size_t read_amd_pages(uint8_t cache_level, ARCH_CACHE_TYPE type, wanted_info info, cpu_cache_callback callback)
{
	if (info == wanted_iterate)
	{
		callback(1, CACHE_TYPE_INSTRUCTION);
		callback(1, CACHE_TYPE_DATA);
		if(read_amd_pages(2, CACHE_TYPE_UNIFIED, wanted_linesize, nullptr) != 0)
			callback(2, CACHE_TYPE_UNIFIED);
		if (read_amd_pages(3, CACHE_TYPE_UNIFIED, wanted_linesize, nullptr) != 0)
			callback(3, CACHE_TYPE_UNIFIED);
		return 0;
	}
	size_t a, b, c, d;
	size_t val = 0;
	if (cache_level == 1)
	{
		x64_cpuid(0x80000005, &a, &b, &c, &d);
		switch (type)
		{
		case CACHE_TYPE_INSTRUCTION:
			val = d;
			break;
		case CACHE_TYPE_DATA:
			val = c;
			break;
		default:
			return 0;
		}
		switch (info)
		{
		case wanted_cachesize:
			return ((val >> 24)&0xFF) * 1024;
		case wanted_associativity:
			val = ((val >> 16) & 0xFF);
			return val == 0xFF ? CACHE_FULLY_ASSOCIATIVE : val;
		case wanted_linesize:
			return (val & 0xFF);
		}
	}
	else if (cache_level == 2 || cache_level == 3)
	{
		if (type != CACHE_TYPE_UNIFIED)
			return 0;
		x64_cpuid(0x80000006, &a, &b, &c, &d);
		size_t val = 0;
		size_t multiplier = 1024;
		if (cache_level == 2)
			val = c;
		else
		{
			val = d;
			multiplier *= 512;
		}
		switch (info)
		{
		case wanted_linesize:
			return val & 0xFF;
		case wanted_cachesize:
			return ((val >> 16) & 0xFFFF)*multiplier;
		case wanted_associativity:
			return amd_80000006_assoc_table[(val >> 12) & 0xF];
		}
	}
	return 0;
}

static size_t cache_info_dispatch(uint8_t cache_level, ARCH_CACHE_TYPE type, wanted_info info, cpu_cache_callback callback = nullptr)
{
	switch (getCpuVendor())
	{
	case VENDOR_INTEL:
		if (max_cpuid_page() >= 4)
		{
			return read_nice_pages(cache_level, type, VENDOR_INTEL, info, callback);
		}
		break;
	case VENDOR_AMD:
		if (max_cpuid_page_extended() >= 0x8000001D)
		{
			//Check topology extensions are actually supported!
			size_t a, b, c, d;
			x64_cpuid(0x80000001, &a, &b, &c, &d, 0);
			if ((c & (1 << 22)) != 0)
			{
				return read_nice_pages(cache_level, type, VENDOR_AMD, info, callback);
			}
		}
		if (max_cpuid_page_extended() >= 0x80000005)
		{
			return read_amd_pages(cache_level, type, info, callback);
		}
		break;
	}
	return 0;
}

size_t cpu_get_cache_size(uint8_t cache_level, ARCH_CACHE_TYPE type)
{
	//Cache size determination is complex	
	return cache_info_dispatch(cache_level, type, wanted_cachesize);
}

size_t cpu_get_cache_associativity(uint8_t cache_level, ARCH_CACHE_TYPE type)
{
	return cache_info_dispatch(cache_level, type, wanted_associativity);
}

size_t cpu_get_cache_linesize(uint8_t cache_level, ARCH_CACHE_TYPE type)
{
	return cache_info_dispatch(cache_level, type, wanted_linesize);
}

size_t iterate_cpu_caches(cpu_cache_callback callback)
{
	return cache_info_dispatch(0, CACHE_TYPE_UNKNOWN, wanted_iterate, callback);
}

void cpu_print_information()
{
	size_t maxcpuid, a, b, c, d;
	x64_cpuid(0, &maxcpuid, &b, &c, &d);
	char vendor[13];
	*(uint32_t*)&vendor[0] = b;
	*(uint32_t*)&vendor[4] = d;
	*(uint32_t*)&vendor[8] = c;
	vendor[12] = 0;
	kprintf(u"CPU vendor: %S\n", vendor);
	if (maxcpuid < 1)
		return;
	x64_cpuid(1, &a, &b, &c, &d);
	size_t family = (a >> 8) & 0xF;
	size_t model = (a >> 4) & 0xF;
	size_t stepping = a & 0xF;
	if (family == 0xF || (family == 0x6 && getCpuVendor() == VENDOR_INTEL))
	{
		family += (a >> 20) & 0xFF;
		model |= (a >> 12) & 0xF0;
	}
	kprintf(u"CPU: family %x, model %x, stepping %x\n", family, model, stepping);
	x64_cpuid(0x80000000, &a, &b, &c, &d);
	if (a < 0x80000004)
		return;
	char brandstring[49];
	x64_cpuid(0x80000002, &a, &b, &c, &d);
	*(uint32_t*)&brandstring[0] = a;
	*(uint32_t*)&brandstring[4] = b;
	*(uint32_t*)&brandstring[8] = c;
	*(uint32_t*)&brandstring[12] = d;
	x64_cpuid(0x80000003, &a, &b, &c, &d);
	*(uint32_t*)&brandstring[16] = a;
	*(uint32_t*)&brandstring[20] = b;
	*(uint32_t*)&brandstring[24] = c;
	*(uint32_t*)&brandstring[28] = d;
	x64_cpuid(0x80000004, &a, &b, &c, &d);
	*(uint32_t*)&brandstring[32] = a;
	*(uint32_t*)&brandstring[36] = b;
	*(uint32_t*)&brandstring[40] = c;
	*(uint32_t*)&brandstring[44] = d;
	brandstring[48] = 0;
	kprintf(u"%S\n", brandstring);
	kprintf(u"L1 instruction cache: %d KB, associativty: %d, line size: %d\n", cpu_get_cache_size(1, CACHE_TYPE_INSTRUCTION) / 1024, \
		cpu_get_cache_associativity(1, CACHE_TYPE_INSTRUCTION), cpu_get_cache_linesize(1, CACHE_TYPE_INSTRUCTION));
	kprintf(u"L1 data cache: %d KB, associativty: %d, line size: %d\n", cpu_get_cache_size(1, CACHE_TYPE_DATA) / 1024, \
		cpu_get_cache_associativity(1, CACHE_TYPE_DATA), cpu_get_cache_linesize(1, CACHE_TYPE_DATA));
	kprintf(u"L2 cache: %d KB, associativty: %d, line size: %d\n", cpu_get_cache_size(2, CACHE_TYPE_UNIFIED) / 1024, \
		cpu_get_cache_associativity(2, CACHE_TYPE_UNIFIED), cpu_get_cache_linesize(2, CACHE_TYPE_UNIFIED));
	kprintf(u"L3 cache: %d MB, associativty: %d, line size: %d\n", cpu_get_cache_size(3, CACHE_TYPE_UNIFIED) / (1024 * 1024), \
		cpu_get_cache_associativity(3, CACHE_TYPE_UNIFIED), cpu_get_cache_linesize(3, CACHE_TYPE_UNIFIED));

	kprintf(u"CR0: %x\n", x64_read_cr0());
	kprintf(u"CR3: %x\n", x64_read_cr3());
	kprintf(u"CR4: %x\n", x64_read_cr4());
}

