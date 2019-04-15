#include <arch/cpu.h>

extern "C" size_t x64_read_cr0();
extern "C" size_t x64_read_cr2();
extern "C" size_t x64_read_cr3();
extern "C" size_t x64_read_cr4();
extern "C" void x64_write_cr0(size_t);
extern "C" void x64_write_cr3(size_t);
extern "C" void x64_write_cr4(size_t);
extern "C" void cpuid(size_t page);

extern "C" void arch_cpu_init()
{
	size_t cr0 = x64_read_cr0();
}