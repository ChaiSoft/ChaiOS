#ifndef CHAIOS_SEMAPHORE_H
#define CHAIOS_SEMAPHORE_H

#include <stdheaders.h>
#include <chaikrnl.h>

typedef void* semaphore_t;

#ifdef __cplusplus
EXTERN{
#else
typedef uint16_t char16_t;
#endif

#ifndef TIMEOUT_INFINITY
#define TIMEOUT_INFINITY SIZE_MAX
#endif

CHAIKRNL_FUNC semaphore_t create_semaphore(size_t count, char16_t* name);
CHAIKRNL_FUNC void delete_semaphore(semaphore_t lock);
CHAIKRNL_FUNC void signal_semaphore(semaphore_t lock, size_t count);
CHAIKRNL_FUNC uint8_t wait_semaphore(semaphore_t lock, size_t count, size_t timeout);
CHAIKRNL_FUNC void write_semaphore(semaphore_t lock, size_t count);
CHAIKRNL_FUNC size_t peek_semaphore(semaphore_t lock);

#ifdef __cplusplus
}
#endif

#endif
