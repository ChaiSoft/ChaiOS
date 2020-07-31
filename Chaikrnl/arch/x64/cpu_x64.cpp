#include <arch/cpu.h>
#include <kstdio.h>
#include <redblack.h>
#include <arch/paging.h>
#include <string.h>
#include <scheduler.h>

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
extern "C" void x64_lidt(void* location);
extern "C" void x64_sidt(void* location);
extern "C" void x64_ltr(uint16_t seg);

typedef void(*fpu_state_proc)(size_t* location);
extern "C" fpu_state_proc x64_save_fpu = nullptr;
extern "C" fpu_state_proc x64_restore_fpu = nullptr;

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
extern "C" void x64_set_breakpoint(void* addr, size_t length, size_t type);
extern "C" void x64_enable_breakpoint(size_t enabled);
extern "C" void x64_pause();
extern "C" void x64_invlpg(void*);
extern "C" void x64_mfence();
extern "C" void x64_cacheflush();
extern "C" uint8_t x64_gs_readb(size_t offset);
extern "C" uint16_t x64_gs_readw(size_t offset);
extern "C" uint32_t x64_gs_readd(size_t offset);
extern "C" uint64_t x64_gs_readq(size_t offset);
extern "C" void x64_gs_writeb(size_t offset, uint8_t val);
extern "C" void x64_gs_writew(size_t offset, uint16_t val);
extern "C" void x64_gs_writed(size_t offset, uint32_t val);
extern "C" void x64_gs_writeq(size_t offset, uint64_t val);
extern "C" uint8_t x64_fs_readb(size_t offset);
extern "C" uint16_t x64_fs_readw(size_t offset);
extern "C" uint32_t x64_fs_readd(size_t offset);
extern "C" uint64_t x64_fs_readq(size_t offset);
extern "C" void x64_fs_writeb(size_t offset, uint8_t val);
extern "C" void x64_fs_writew(size_t offset, uint16_t val);
extern "C" void x64_fs_writed(size_t offset, uint32_t val);
extern "C" void x64_fs_writeq(size_t offset, uint64_t val);
extern "C" size_t x64_context_size;
extern "C" int x64_save_context(context_t context);
extern "C" void x64_load_context(context_t context, int value);
extern "C" void x64_new_context(context_t context, void* stackptr, void* entry);
extern "C" void x64_hlt();

extern "C" uint16_t x64_bswapw(uint16_t);
extern "C" uint32_t x64_bswapd(uint32_t);
extern "C" uint64_t x64_bswapq(uint64_t);

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
extern "C" size_t x64_avx_level = 0;

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

#define MSR_IA32_MTRRCAP 0xFE
#define MSR_IA32_MTRRBASE 0x200
#define MSR_IA32_PAT 0x277
#define MSR_IA32_MTRR_DEF_TYPE 0x2FF

#define MSR_IA32_MISC_ENABLE 0x1A0
#define MSR_IA32_ENERGY_PERF_BIAS 0x1B0
#define MSR_IA32_PERF_CTL 0x199
#define MSR_IA32_PERF_STATUS 0x198
#define MSR_IA32_PM_ENABLE 0x770
#define MSR_IA32_HWP_CAPABILITIES 0x771
#define MSR_IA32_HWP_REQUEST_PKG 0x772
#define MSR_IA32_HWP_INTERRUPT 0x773
#define MSR_IA32_HWP_REQUEST 0x774
#define MSR_IA32_HWP_PECI_REQUEST_INFO 0x775
#define MSR_IA32_HWP_STATUS 0x777
#define MSR_IA32_FS_BASE 0xC0000100
#define MSR_IA32_GS_BASE 0xC0000101
#define MSR_IA32_KERNELGS_BASE 0xC0000102

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

static void x64_performance_features()
{
	size_t max_cpuid = max_cpuid_page();
	uint64_t misc_enable = x64_rdmsr(MSR_IA32_MISC_ENABLE);
	if (max_cpuid >= 1)
	{
		size_t a, b, c, d;
		x64_cpuid(1, &a, &b, &c, &d);
		if ((c & (1 << 7)) != 0)
		{
			//SpeedStep
			misc_enable |= (1 << 16);
		}
	}
	if (max_cpuid >= 6)
	{
		//Thermal and performance page
		size_t a, b, c, d;
		x64_cpuid(6, &a, &b, &c, &d);
		if ((a & (1 << 1)) != 0)
		{
			//TurboBoost
			uint64_t val = x64_rdmsr(MSR_IA32_PERF_CTL) & 0xFFFF;
			x64_wrmsr(MSR_IA32_PERF_CTL, val);
		}
		if ((c & (1 << 3)) != 0)
		{
			//We have the performance msr
			x64_wrmsr(MSR_IA32_ENERGY_PERF_BIAS, 0);	//Maximum performance
		}
		if ((a & (1 << 7)) != 0)
		{
			//HWP
			//Disable interrupts for now
			if((a & (1<<8)) != 0)
				x64_wrmsr(MSR_IA32_HWP_INTERRUPT, 0);
			x64_wrmsr(MSR_IA32_PM_ENABLE, 1);
			//Set for highest performance
			uint64_t perf_caps = x64_rdmsr(MSR_IA32_HWP_CAPABILITIES);
			uint8_t max_perf_value = perf_caps & 0xFF;
			uint8_t min_perf_value = (perf_caps >> 24) & 0xFF;
			uint64_t requested_perf = min_perf_value | ((size_t)max_perf_value << 8);
			//Bits 24 to 31 0 for maximum perfomance, 32 to 41 0 for hardware calculation
			x64_wrmsr(MSR_IA32_HWP_REQUEST, requested_perf);
			if((a & (1<<11)) != 0)
				x64_wrmsr(MSR_IA32_HWP_REQUEST_PKG, requested_perf);
			if ((a & (1 << 16)) != 0)
			{
				uint64_t peci = x64_rdmsr(MSR_IA32_HWP_PECI_REQUEST_INFO);
				if ((peci & (1ui64 << 63)) != 0)
				{
					kprintf(u"PECI minimum override active: %d\n", peci & 0xFF);
				}
				if ((peci & (1ui64 << 62)) != 0)
				{
					kprintf(u"PECI maximum override active: %d\n", (peci>>8) & 0xFF);
				}
				if ((peci & (1ui64 << 60)) != 0)
				{
					kprintf(u"PECI preference override active: %d\n", (peci >> 24) & 0xFF);
				}
			}
		}
	}
	x64_wrmsr(MSR_IA32_MISC_ENABLE, misc_enable);
}

extern "C" void arch_cpu_init()
{
	size_t cr0 = x64_read_cr0();
	//Enable FPU
	cr0 |= (1 << 1) | (1<< 5);
	cr0 &= (SIZE_MAX - (1 << 2) - (1 << 3) - (1<<30) - (1<<29));		//1<<30 turns off cache disable
	cr0 |= (1 << 16);		//Can't write read-only pages in ring 0
#if 0
	//DEBUG: DISABLE CACHE
	cr0 |= (1 << 30);
#endif
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
		static fpu_state_proc best_save = nullptr;
		static fpu_state_proc best_restore = nullptr;
		static size_t saveareasz = 0;
		if ((d & (1 << 24)) != 0)
		{
			//FXSAVE support
			cr4 |= (1 << 9);
			best_save = &x64_fxsave;
			best_restore = &x64_fxrstor;
			saveareasz = 512;
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
			{
				xcr0 |= 7;		//AVX enable
				x64_avx_level = 1;
			}
			//Check for AVX-512
			if (max_cpuid >= 0xD)
			{
				size_t ax, bx, cx, dx;
				x64_cpuid(0xD, &ax, &bx, &cx, &dx, 0);
				xcr0 |= (ax&(7<<5));		//All supported bits of AVX 512 state
				saveareasz = bx;		//Maximum xsave area size
			}
			else
			{
				saveareasz = ((xcr0 & 4) != 0) ? 576 : 1088;
			}
			best_save = &x64_xsave;
			best_restore = &x64_xrstor;
			x64_write_xcr0(xcr0);
			xmask = xcr0;
		}
		//Debugging extensions
		cr4 |= (1 << 3);
		x64_write_cr4(cr4);
		if (arch_is_bsp())
		{
			x64_save_fpu = best_save;
			x64_restore_fpu = best_restore;
			xsavearea_size = saveareasz;
		}
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
	//Performance stuff
	x64_performance_features();
	if (!arch_is_bsp())
	{
		//Copy the GDT
		gdtr* new_gdtr = new gdtr;
		gdt_entry* new_gdt = new gdt_entry[GDT_ENTRIES];
		fill_gdt(new_gdt);
		new_gdtr->gdtaddr = new_gdt;
		new_gdtr->size = GDT_ENTRIES * sizeof(gdt_entry) - 1;
		x64_lgdt(new_gdtr);
	}
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
cpu_status_t arch_enable_interrupts()
{
	auto val = arch_disable_interrupts();
	arch_restore_state(val | 0x200);
	return val;
}

void arch_set_breakpoint(void* addr, size_t length, size_t type)
{
	x64_set_breakpoint(addr, length, type);
}

void arch_enable_breakpoint(size_t enabled)
{
	x64_enable_breakpoint(enabled);
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

void arch_set_paging_root(size_t root)
{
	x64_write_cr3(root);
}

#pragma pack(push, 1)
struct IDT {
	uint16_t offset_1; // offset bits 0..15
	uint16_t selector; // a code segment selector in GDT or LDT
	uint8_t ist;       // bits 0..2 holds Interrupt Stack Table offset, rest of bits zero.
	uint8_t type_attr; // type and attributes
	uint16_t offset_2; // offset bits 16..31
	uint32_t offset_3; // offset bits 32..63
	uint32_t zero;     // reserved
};
struct IDTR {
	uint16_t length;
	void* idtaddr;
};
struct TSS {
	uint32_t reserved;
	size_t rsp[3];
	size_t reserved2;
	size_t IST[7];
	size_t resv3;
	uint16_t resv4;
	uint16_t iomapbase;
};
struct stack_frame {
	stack_frame* baseptr;
	size_t rip;
};
struct interrupt_stack_frame {
	stack_frame* baseptr;
	size_t error;
	size_t rip;
	size_t cs;
	size_t rflags;
	size_t rsp;
	size_t ss;
};
#pragma pack(pop)

static void register_irq(IDT* entry, void* fn)
{
	size_t faddr = (size_t)fn;
	entry->offset_1 = faddr & UINT16_MAX;
	entry->offset_2 = (faddr >> 16) & UINT16_MAX;
	entry->offset_3 = (faddr >> 32) & UINT32_MAX;
}

static RedBlackTree<uint32_t, arch_interrupt_subsystem*> interrupt_subsystems;

extern "C" extern void* default_irq_handlers[];
struct dispatch_data {
	void* func;
	void* param;
	void(*post_event)();
};
typedef RedBlackTree<uint32_t, dispatch_data*> dispatch_tree;
typedef dispatch_tree* cpu_dispatch;
static cpu_dispatch dispatch_funcs[256] = { nullptr };

extern "C" void x64_interrupt_dispatcher(size_t vector, interrupt_stack_frame* istack_frame)
{
	uint32_t previrql = pcpu_data.irql;
	pcpu_data.irql = IRQL_INTERRUPT;
	auto treeptr = dispatch_funcs[vector];
	bool valid = (treeptr != nullptr);
	if (valid)
	{
		valid = (treeptr->find(pcpu_data.cpuid) != treeptr->end());
	}
	if (!valid)
	{
		kprintf(u"An unknown interrupt occurred: %x\n", vector);
		kprintf(u"Stack frame: %x\n", istack_frame);
		kprintf(u"Error code: %x\n", istack_frame->error);
		kprintf(u"Return address: %x:%x\n", istack_frame->cs, istack_frame->rip);
		kprintf(u"CPU %d, thread %x\n", pcpu_data.cpuid, pcpu_data.runningthread);
		kprintf(u" Base Pointer: %x\n", istack_frame->baseptr);
#if 0
		stack_frame* prevframe = istack_frame->baseptr;
		kprintf(u"Stack trace:\n");
		for (int i = 0; i < 5 && ((size_t)prevframe > 0xFFFF000000000000); ++i)
		{
			kprintf(u" Return Address: %x\n", prevframe->rip);
			prevframe = prevframe->baseptr;
		}
#endif
		while (1);
	}
	dispatch_data* dispdata = (*treeptr)[pcpu_data.cpuid];
	dispatch_interrupt_handler handler = reinterpret_cast<dispatch_interrupt_handler>(dispdata->func);
	void* param = dispdata->param;
	if (!param)
		param = istack_frame;
	handler(vector, param);
	if (dispdata->post_event)
		dispdata->post_event();
	pcpu_data.irql = previrql;
}

extern "C" uint8_t page_fault_handler(size_t vector, void* param)
{
	interrupt_stack_frame* frame = (interrupt_stack_frame*)param;
	kputs(u"A page fault occurred\n");
	kprintf(u"Error accessing address %x\n", x64_read_cr2());
	kprintf(u"Error code: %x\n", frame->error);
	kprintf(u"Return address: %x:%x\n", frame->cs, frame->rip);
	kprintf(u"CPU %d, thread %x\n", pcpu_data.cpuid, pcpu_data.runningthread);
	while (1);
}

void register_native_irq(size_t vector, uint32_t processor, void* fn, void* param)
{
	arch_reserve_interrupt_range(vector, vector);
	IDTR current_idt;
	x64_sidt(&current_idt);
	IDT* idt = reinterpret_cast<IDT*>(current_idt.idtaddr);
	register_irq(&idt[vector], fn);
}

void register_native_postevt(size_t vector, uint32_t processor, void(*evt)())
{
	//Unsupported
}

static arch_interrupt_subsystem subsystem_native
{
	&register_native_irq,
	&register_native_postevt
};

void register_dispatch_irq(size_t vector, uint32_t processor, void* fn, void* param)
{
	uint32_t cpuid = pcpu_data.cpuid;
	if (processor != cpuid && processor != INTERRUPT_CURRENTCPU)
	{
		//Tell the AP to reserve the interrupt range
		//kprintf(u"Failed to register interrupt %x, CPU %d, running %d\n", vector, processor, cpuid);
	}
	else
	{
		arch_reserve_interrupt_range(vector, vector);
	}
	dispatch_tree*& disptree = dispatch_funcs[vector];
	if (!disptree)
		disptree = new dispatch_tree;
	dispatch_data* disp;
	if (disptree->find(cpuid) == disptree->end())
		disp = (*disptree)[cpuid] = new dispatch_data;
	else
		disp = (*disptree)[cpuid];
	disp->func = fn;
	disp->param = param;
	disp->post_event = nullptr;
}

void register_dispatch_postevt(size_t vector, uint32_t processor, void(*evt)())
{
	uint32_t cpuid = pcpu_data.cpuid;
	if (processor != cpuid && processor != INTERRUPT_CURRENTCPU)
		return;
	dispatch_tree*& disptree = dispatch_funcs[vector];
	if (!disptree)
		disptree = new dispatch_tree;
	(*disptree)[cpuid]->post_event = evt;
}

static arch_interrupt_subsystem subsystem_dispatch
{
	&register_dispatch_irq,
	&register_dispatch_postevt
};

#include <arch/x64/apic.h>

static volatile bool bsp_set = false;
static volatile uint32_t bsp = 0;
uint8_t arch_is_bsp()
{
	if (!bsp_set)
	{
		bsp_set = true;
		bsp = arch_current_processor_id();
	}
	return bsp == arch_current_processor_id();
}

uint64_t arch_read_per_cpu_data(uint32_t offset, uint8_t width)
{
	switch (width)
	{
	case 8:
		return x64_fs_readb(offset);
	case 16:
		return x64_fs_readw(offset);
	case 32:
		return x64_fs_readd(offset);
	case 64:
		return x64_fs_readq(offset);
	}
	return 0;
}
void arch_write_per_cpu_data(uint32_t offset, uint8_t width, uint64_t value)
{
	switch (width)
	{
	case 8:
		return x64_fs_writeb(offset, value);
	case 16:
		return x64_fs_writew(offset, value);
	case 32:
		return x64_fs_writed(offset, value);
	case 64:
		return x64_fs_writeq(offset, value);
	}
}

void arch_write_tls_base(void* tls, uint8_t user)
{
	if(user != 0)
		x64_wrmsr(MSR_IA32_KERNELGS_BASE, (size_t)tls);
	else
		x64_wrmsr(MSR_IA32_GS_BASE, (size_t)tls);
}

uint64_t arch_read_tls(uint32_t offset, uint8_t user, uint8_t width)
{
	if (user != 0)
	{
		void* tls = (void*)x64_rdmsr(MSR_IA32_KERNELGS_BASE);
		switch (width)
		{
		case 8:
			return *raw_offset<uint8_t*>(tls, offset);
		case 16:
			return *raw_offset<uint16_t*>(tls, offset);
		case 32:
			return *raw_offset<uint32_t*>(tls, offset);
		case 64:
			return *raw_offset<uint64_t*>(tls, offset);
		}
		return 0;
	}
	else
	{
		switch (width)
		{
		case 8:
			return x64_gs_readb(offset);
		case 16:
			return x64_gs_readw(offset);
		case 32:
			return x64_gs_readd(offset);
		case 64:
			return x64_gs_readq(offset);
		}
		return 0;
	}
}
void arch_write_tls(uint32_t offset, uint8_t user, uint64_t value, uint8_t width)
{
	if (user != 0)
	{
		void* tls = (void*)x64_rdmsr(MSR_IA32_KERNELGS_BASE);
		switch (width)
		{
		case 8:
			*raw_offset<uint8_t*>(tls, offset) = value;
			return;
		case 16:
			*raw_offset<uint16_t*>(tls, offset) = value;
			return;
		case 32:
			*raw_offset<uint32_t*>(tls, offset) = value;
			return;
		case 64:
			*raw_offset<uint64_t*>(tls, offset) = value;
			return;
		}
	}
	else
	{
		switch (width)
		{
		case 8:
			return x64_gs_writeb(offset, value);
		case 16:
			return x64_gs_writew(offset, value);
		case 32:
			return x64_gs_writed(offset, value);
		case 64:
			return x64_gs_writeq(offset, value);
		}
	}
}

struct arch_per_cpu_data {
	per_cpu_data public_data;
	uint8_t* interruptsavailmap;
};

struct arch_tls_data {
	arch_tls_data* selfptr;
};

static_assert(sizeof(per_cpu_data) <= 0x30, "Please reallocate");
#define PCPU_DATA_AVAILINTS 0x30

void arch_setup_interrupts()
{
	arch_disable_interrupts();
	//Create the TSS for this CPU
	TSS* cpu_tss = new TSS;
	cpu_tss->iomapbase = sizeof(TSS);
	size_t tss_addr = (size_t)cpu_tss;
	gdtr curr_gdt;
	x64_sgdt(&curr_gdt);
	gdt_entry* the_gdt = curr_gdt.gdtaddr;
	set_gdt_entry(the_gdt[GDT_ENTRY_TSS], tss_addr & UINT32_MAX, sizeof(TSS), GDT_ACCESS_PRESENT | 0x9, 0);
	*(uint64_t*)&the_gdt[GDT_ENTRY_TSS + 1] = (tss_addr >> 32);
	x64_ltr(SEGVAL(GDT_ENTRY_TSS, 0));
	//Create per-cpu structure
	arch_per_cpu_data* cpu_data = new arch_per_cpu_data;
	cpu_data->public_data.cpu_data = &cpu_data->public_data;
	//Hidden per CPU interrupt vector map
	uint8_t* interruptsavailmap = new uint8_t[256 / 8];
	memset(interruptsavailmap, 0xFF, 256 / 8);
	//Because we're in kernel mode, GS is active
	x64_wrmsr(MSR_IA32_FS_BASE, (size_t)cpu_data);
	x64_wrmsr(MSR_IA32_KERNELGS_BASE, 0);
	pcpu_data.cpuid = arch_current_processor_id();
	pcpu_data.runningthread = 0;
	arch_write_per_cpu_data(PCPU_DATA_AVAILINTS, 64, (size_t)interruptsavailmap);
	pcpu_data.irql = IRQL_KERNEL;
	//Setup an IDT. We maintain a seperate IDT for each CPU, so allocation is dynamic
	IDT* the_idt = new IDT[256];
	IDTR* idtr = new IDTR;
	idtr->idtaddr = the_idt;
	idtr->length = 256 * sizeof(IDT) - 1;
	x64_lidt(idtr);
	for (size_t n = 0; n < 256; ++n)
	{
		the_idt[n].ist = 0;
		the_idt[n].selector = SEGVAL(GDT_ENTRY_KERNEL_CODE, 0);
		the_idt[n].zero = 0;
		the_idt[n].type_attr = GDT_ACCESS_PRESENT | 0xE;
		register_irq(&the_idt[n], default_irq_handlers[n]);
	}
	delete idtr;
	if (arch_is_bsp())
	{
		//Register interrupt subsystems
		arch_register_interrupt_subsystem(INTERRUPT_SUBSYSTEM_NATIVE, &subsystem_native);
		arch_register_interrupt_subsystem(INTERRUPT_SUBSYSTEM_DISPATCH, &subsystem_dispatch);
	}
	//Reserve exceptions
	arch_reserve_interrupt_range(0, 31);
	//Now set up the interrupt controller, so we don't die for no apparent reason
	x64_init_apic();
	x64_restore_flags(x64_disable_interrupts() | (1<<9));
	arch_register_interrupt_handler(INTERRUPT_SUBSYSTEM_DISPATCH, 0xE, INTERRUPT_CURRENTCPU, &page_fault_handler, nullptr);
}

CHAIKRNL_FUNC void arch_register_interrupt_subsystem(uint32_t subsystem, arch_interrupt_subsystem* system)
{
	interrupt_subsystems[subsystem] = system;
}

CHAIKRNL_FUNC void arch_register_interrupt_handler(uint32_t subsystem, size_t vector, uint32_t processor, void* fn, void* param)
{
	interrupt_subsystems[subsystem]->register_irq(vector, processor, fn, param);
}

CHAIKRNL_FUNC void arch_install_interrupt_post_event(uint32_t subsystem, size_t vector, uint32_t processor, void(*evt)())
{
	interrupt_subsystems[subsystem]->post_evt(vector, processor, evt);
}

CHAIKRNL_FUNC uint32_t arch_allocate_interrupt_vector()
{
	auto st = arch_disable_interrupts();		//So we don't get interfered with
	uint8_t* availints = (uint8_t*)arch_read_per_cpu_data(PCPU_DATA_AVAILINTS, 64);
	uint_fast8_t i = 0;
	uint32_t ret = -1;
	for (; availints[i] == 0 && i < (256 / 8); ++i);
	if (i == 256 / 8)
		goto end;
	else
	{
		uint8_t current = availints[i];
		size_t off = 0;
		for (; off < 8 && (current & (1 << off) != 0); ++off);
		ret = i * 8 + off;		
	}
end:
	if(ret != -1)
		arch_reserve_interrupt_range(ret, ret);
	arch_restore_state(st);
	return ret;
}
CHAIKRNL_FUNC void arch_reserve_interrupt_range(uint32_t start, uint32_t end)
{
	auto st = arch_disable_interrupts();		//So we don't get interfered with
	uint8_t* availints = (uint8_t*)arch_read_per_cpu_data(PCPU_DATA_AVAILINTS, 64);
	size_t i = start / 8;
	size_t o = start % 8;
	while (i * 8 + o <= end)
	{
		availints[i] &= (UINT8_MAX - (1 << o));
		++o;
		i += o / 8;
		o = o % 8;
	}


	arch_restore_state(st);
}

paddr_t arch_msi_address(uint64_t* data, size_t vector, uint32_t processor, uint8_t edgetrigger, uint8_t deassert)
{
	*data = (vector & 0xFF) | (edgetrigger == 1 ? 0 : (1 << 15)) | (deassert == 1 ? 0 : (1 << 14));
	return (0xFEE00000 | (processor << 12));
}

context_t context_factory()
{
	return (context_t)new uint8_t[x64_context_size];
}
void context_destroy(context_t ctx)
{
	delete[](uint8_t*) ctx;
}
int save_context(context_t ctxt)
{
	return x64_save_context(ctxt);
}
void jump_context(context_t ctxt, int value)
{
	x64_load_context(ctxt, value);
}

void arch_halt()
{
	x64_hlt();
}

CHAIKRNL_FUNC uint16_t arch_swap_endian16(uint16_t v)
{
	return x64_bswapw(v);
}
CHAIKRNL_FUNC uint32_t arch_swap_endian32(uint32_t v)
{
	return x64_bswapd(v);
}
CHAIKRNL_FUNC uint64_t arch_swap_endian64(uint64_t v)
{
	return x64_bswapq(v);
}

static const size_t PAGES_STACK = 16;

#include <liballoc.h>
kstack_t arch_create_kernel_stack()
{
	void* kstack = kmalloc(PAGES_STACK*PAGESIZE);
	return kstack;
}

void arch_destroy_kernel_stack(kstack_t stack)
{
	kfree(stack);
}
void* arch_init_stackptr(kstack_t stack)
{
	return raw_offset<void*>(stack, PAGESIZE *PAGES_STACK - sizeof(void*));
}

void arch_new_thread(context_t ctxt, kstack_t stack, void* entrypt)
{
	void* sp = arch_init_stackptr(stack);
	x64_new_context(ctxt, sp, entrypt);
}

void cpu_print_information()
{
	kprintf(u"X64 context size: %d\n", x64_context_size);
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

