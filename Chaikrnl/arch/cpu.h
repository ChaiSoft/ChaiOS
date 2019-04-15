#ifndef CHAIOS_ARCH_CPU_H
#define CHAIOS_ARCH_CPU_H

#include <stdheaders.h>
#include <chaikrnl.h>

#ifdef __cplusplus
extern "C" {
#endif

void arch_cpu_init();

size_t arch_read_port(size_t port, uint8_t width);
void arch_write_port(size_t port, size_t value, uint8_t width);

#ifdef __cplusplus
bool arch_cas(volatile size_t* loc, size_t oldv, size_t newv);
#else
int arch_cas(volatile size_t* loc, size_t oldv, size_t newv);
#endif

void arch_pause();		//Hyperthreading hint

typedef size_t cpu_status_t;

cpu_status_t arch_disable_interrupts();
void arch_restore_state(cpu_status_t val);

void arch_flush_tlb(void*);
CHAIKRNL_FUNC void arch_flush_cache();
void arch_memory_barrier();

#ifdef __cplusplus
enum ARCH_CACHE_TYPE {
	CACHE_TYPE_UNKNOWN,
	CACHE_TYPE_DATA,
	CACHE_TYPE_INSTRUCTION,
	CACHE_TYPE_UNIFIED
};

#define CACHE_FULLY_ASSOCIATIVE SIZE_MAX

size_t cpu_get_cache_size(uint8_t cache_level, ARCH_CACHE_TYPE type);
size_t cpu_get_cache_associativity(uint8_t cache_level, ARCH_CACHE_TYPE type);
size_t cpu_get_cache_linesize(uint8_t cache_level, ARCH_CACHE_TYPE type);

typedef void(*cpu_cache_callback)(uint8_t, ARCH_CACHE_TYPE);
size_t iterate_cpu_caches(cpu_cache_callback callback);
#endif

void cpu_print_information();

#ifdef __cplusplus
}
#endif

#endif
