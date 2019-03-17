#ifndef CHAIOS_ASSEMBLY_H
#define CHAIOS_ASSEMBLY_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

uint8_t inportb(uint16_t port);
uint16_t inportw(uint16_t port);
uint32_t inportd(uint16_t port);
void outportb(uint16_t port, uint8_t val);
void outportw(uint16_t port, uint16_t val);
void outportd(uint16_t port, uint32_t val);
void cpuid(size_t page, size_t* a, size_t* b, size_t* c, size_t* d, size_t subpage=0);
void cacheflush();

#ifdef __cplusplus
}
#endif

#endif

