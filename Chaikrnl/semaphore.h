#ifndef CHAIOS_SEMAPHORE_H
#define CHAIOS_SEMAPHORE_H

#include <stdheaders.h>
#include <chaikrnl.h>

typedef void* semaphore_t;

#ifdef __cplusplus
EXTERN{
#endif

CHAIKRNL_FUNC semaphore_t create_semaphore(size_t count);
CHAIKRNL_FUNC void delete_semaphore(semaphore_t lock);
CHAIKRNL_FUNC void signal_semaphore(semaphore_t lock, size_t count);
CHAIKRNL_FUNC uint8_t wait_semaphore(semaphore_t lock, size_t count, size_t timeout);

#ifdef __cplusplus
}
#endif

#endif
