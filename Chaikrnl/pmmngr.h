#ifndef CHAIOS_PMMNGR_H
#define CHAIOS_PMMNGR_H
#include <kernelinfo.h>

#define PAGESIZE 4096

#define KB 1024
#define MB (1024*KB)
#define GB (1024*MB)
#define TB (1024*GB)

#if defined X86 || defined X64
#define ARCH_PHY_REGION_ISADMA 0
#define ARCH_PHY_REGION_NORMAL 1
#ifdef X64
#define ARCH_PHY_REGION_PCIDMA 2
#else
#define ARCH_PHY_REGION_PCIDMA ARCH_PHY_REGION_NORMAL
#endif
#define ARCH_PHY_REGIONS_MAX (ARCH_PHY_REGION_PCIDMA + 1)
#endif

typedef uint32_t numa_t;
#define NUMA_STRIPE UINT32_MAX

typedef uint32_t cache_colour;
#define CACHE_COLOUR_NONE UINT32_MAX

typedef uint64_t paddr_t;
void initialize_pmmngr(PMMNGR_INFO& info);
void startup_pmmngr(BootType mmaptype, void* memmap);
paddr_t pmmngr_allocate(size_t pages, uint8_t region = ARCH_PHY_REGION_NORMAL, numa_t numa_domain = NUMA_STRIPE, cache_colour colour = CACHE_COLOUR_NONE);
void pmmngr_free(paddr_t addr, size_t length);
#endif