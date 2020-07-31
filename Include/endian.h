#ifndef CHAIOS_CHAIKRNL_ENDIAN_H
#define CHAIOS_CHAIKRNL_ENDIAN_H

#include <stdint.h>
#include "chaikrnl.h"
#include <arch/cpu.h>

#if defined(X64) || defined(X86)
#define LITTLE_ENDIAN
#else
#error "Unknown Architecture Endianess"
#endif

#ifdef LITTLE_ENDIAN
typedef uint16_t uint16_le;
typedef uint32_t uint32_le;
typedef uint64_t uint64_le;

typedef struct {
	uint16_t v;
}uint16_be;
static_assert(sizeof(uint16_be) == 2, "Struct Alignment Incompatible");
typedef struct {
	uint32_t v;
}uint32_be;
static_assert(sizeof(uint32_be) == 4, "Struct Alignment Incompatible");
typedef struct {
	uint64_t v;
}uint64_be;
static_assert(sizeof(uint64_be) == 8, "Struct Alignment Incompatible");
#else
typedef uint16_t uint16_be;
typedef uint32_t uint32_be;
typedef uint64_t uint64_be;

typedef struct {
	uint16_t v;
}uint16_le;
static_assert(sizeof(uint16_le) == 2, "Struct Alignment Incompatible");
typedef struct {
	uint32_t v;
}uint32_le;
static_assert(sizeof(uint32_le) == 4, "Struct Alignment Incompatible");
typedef struct {
	uint64_t v;
}uint64_le;
static_assert(sizeof(uint64_le) == 8, "Struct Alignment Incompatible");
#endif

#ifdef LITTLE_ENDIAN
#define LE_TO_CPU16(x) x
#define LE_TO_CPU32(x) x
#define LE_TO_CPU64(x) x

#define CPU_TO_LE16(x) x
#define CPU_TO_LE32(x) x
#define CPU_TO_LE64(x) x

#define BE_TO_CPU16(x) arch_swap_endian16(x.v)
#define BE_TO_CPU32(x) arch_swap_endian32(x.v)
#define BE_TO_CPU64(x) arch_swap_endian64(x.v)

#define CPU_TO_BE16(x) {arch_swap_endian16(x)}
#define CPU_TO_BE32(x) {arch_swap_endian32(x)}
#define CPU_TO_BE64(x) {arch_swap_endian64(x)}
#else
#define BE_TO_CPU16(x) x
#define BE_TO_CPU32(x) x
#define BE_TO_CPU64(x) x

#define CPU_TO_BE16(x) x
#define CPU_TO_BE32(x) x
#define CPU_TO_BE64(x) x

#define LE_TO_CPU16(x) arch_swap_endian16(x.v)
#define LE_TO_CPU32(x) arch_swap_endian32(x.v)
#define LE_TO_CPU64(x) arch_swap_endian64(x.v)

#define CPU_TO_LE16(x) {arch_swap_endian16(x)}
#define CPU_TO_LE32(x) {arch_swap_endian32(x)}
#define CPU_TO_LE64(x) {arch_swap_endian64(x)}
#endif

#endif
