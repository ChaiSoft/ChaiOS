#include <arch/paging.h>
#include <arch/cpu.h>

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_BSP 0x100 // Processor is a BSP
#define IA32_APIC_BASE_MSR_X2APIC 0x400
#define IA32_APIC_BASE_MSR_ENABLE 0x800

extern "C" uint64_t x64_rdmsr(size_t msr);
extern "C" void x64_wrmsr(size_t msr, uint64_t);

static void x64_init_apic()
{
	//First of all, disable the PIC
	arch_write_port(0xA1, 0xFF, 8);
	arch_write_port(0x21, 0xFF, 8);
	//The PIC is now disabled. Enable the LAPIC
	uint64_t apic_base = x64_rdmsr(IA32_APIC_BASE_MSR) & (UINT64_MAX - 0xFFF);

}