#ifndef CHAIOS_ASSEMBLY_H
#define CHAIOS_ASSEMBLY_H

#include <stdheaders.h>

#define EXTERN extern "C"

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t cpu_status_t;

uint8_t inportb(uint16_t port);
uint16_t inportw(uint16_t port);
uint32_t inportd(uint16_t port);
void outportb(uint16_t port, uint8_t val);
void outportw(uint16_t port, uint16_t val);
void outportd(uint16_t port, uint32_t val);
#ifdef __cplusplus
void cpuid(size_t page, size_t* a, size_t* b, size_t* c, size_t* d, size_t subpage=0);
#else
void cpuid(size_t page, size_t* a, size_t* b, size_t* c, size_t* d, size_t subpage);
#endif
void cacheflush();
void tlbflush(void* addr);
void pause();
int compareswap(volatile size_t* location, size_t oldv, size_t newv);
cpu_status_t disable();
void restore(cpu_status_t flags);
void memory_barrier();
void* get_paging_root();
void set_paging_root(void*);
void cpu_startup();
void save_gdt(void*);
size_t get_segment_register(uint8_t index);
uint64_t rdmsr(size_t reg);
void wrmsr(size_t reg, uint64_t val);
size_t read_cr0();
void write_cr0(size_t);
void call_kernel(void* param, void* entry, void* stack, size_t stacksz);

#ifdef __cplusplus
}
#endif

#endif

