#include "spinlock.h"
#include "assembly.h"
#include "liballoc.h"

typedef struct _spinlock {
	size_t value;
}spinlock, *pspinlock;

EXTERN spinlock_t create_spinlock()
{
	pspinlock lock = new spinlock;
	lock->value = 0;
	return (spinlock_t)lock;
}
EXTERN void delete_spinlock(spinlock_t lock)
{
	delete lock;
}
EXTERN cpu_status_t acquire_spinlock(spinlock_t lock)
{
	pspinlock slock = (pspinlock)lock;
	cpu_status_t v = disable();
	//We won't be preempted, so we're only contending with other CPUs
	while (!compareswap(&slock->value, 0, 1));
	return v;
}
EXTERN void release_spinlock(spinlock_t lock, cpu_status_t status)
{
	pspinlock slock = (pspinlock)lock;
	if (!compareswap(&slock->value, 1, 0))
		slock->value = 0;		//Should never be necessary, but this prevents freezing due to memory corruption.
	restore(status);
}