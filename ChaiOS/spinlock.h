#ifndef CHAIOS_SPINLOCK_H
#define CHAIOS_SPINLOCK_H

#include <stdint.h>
#include "assembly.h"

typedef void* spinlock_t;

#ifdef __cplusplus
EXTERN {
#endif


spinlock_t create_spinlock();
void delete_spinlock(spinlock_t lock);
cpu_status_t acquire_spinlock(spinlock_t lock);
void release_spinlock(spinlock_t lock, cpu_status_t status);

#ifdef __cplusplus
}
#endif

#endif
