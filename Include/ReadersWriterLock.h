#ifndef CHAIOS_READERS_WRITER_LOCK_H
#define CHAIOS_READERS_WRITER_LOCK_H

#include <stdheaders.h>
#include <chaikrnl.h>
#include <arch/cpu.h>

typedef void* sharespinlock_t;

#ifdef __cplusplus
EXTERN{
#endif


CHAIKRNL_FUNC sharespinlock_t SharedSpinlockCreate();
CHAIKRNL_FUNC void SharedSpinlockDelete(sharespinlock_t lock);
CHAIKRNL_FUNC cpu_status_t SharedSpinlockAcquire(sharespinlock_t lock, BOOL exclusive);
CHAIKRNL_FUNC void SharedSpinlockRelease(sharespinlock_t lock, cpu_status_t status);

#ifdef __cplusplus
}
#endif

#endif