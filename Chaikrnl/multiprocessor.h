#ifndef CHAIOS_MULTIPROCESSOR_H
#define CHAIOS_MULTIPROCESSOR_H

#include <stdheaders.h>
#include <chaikrnl.h>

void startup_multiprocessor();

typedef void(*ap_routine)(void*);
void ap_run_routine(uint32_t cpuid, ap_routine routine, void* data);

typedef bool(*ap_callback)(uint32_t id);
void iterate_aps(ap_callback callback);
EXTERN CHAIKRNL_FUNC size_t CpuCount();
/*
CpuIdentLogical - returns a unique, zero-based processor ID from architectural identifier
*/
EXTERN CHAIKRNL_FUNC size_t CpuCurrentLogicalId();
EXTERN CHAIKRNL_FUNC size_t CpuIdentLogical(size_t archIdent);
EXTERN CHAIKRNL_FUNC size_t CpuIdentArchitectural(size_t logicalIdent);

#endif
