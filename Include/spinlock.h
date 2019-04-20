#ifndef CHAIOS_SPINLOCK_H
#define CHAIOS_SPINLOCK_H

#include <stdheaders.h>
#include <chaikrnl.h>
#include <arch/cpu.h>

typedef void* spinlock_t;

#ifdef __cplusplus
EXTERN {
#endif


CHAIKRNL_FUNC spinlock_t create_spinlock();
CHAIKRNL_FUNC spinlock_t get_static_spinlock();
CHAIKRNL_FUNC void delete_spinlock(spinlock_t lock);
CHAIKRNL_FUNC cpu_status_t acquire_spinlock(spinlock_t lock);
CHAIKRNL_FUNC void release_spinlock(spinlock_t lock, cpu_status_t status);

#ifdef __cplusplus
}
#endif

#endif
