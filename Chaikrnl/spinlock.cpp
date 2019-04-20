#include <spinlock.h>
#include <arch/cpu.h>

typedef struct _spinlock {
	size_t value;
}spinlock, *pspinlock;

static spinlock s_lock;

EXTERN CHAIKRNL_FUNC spinlock_t get_static_spinlock()
{
	return (spinlock_t)&s_lock;
}

EXTERN CHAIKRNL_FUNC spinlock_t create_spinlock()
{
	pspinlock lock = new spinlock;
	lock->value = 0;
	return (spinlock_t)lock;
}
EXTERN CHAIKRNL_FUNC void delete_spinlock(spinlock_t lock)
{
	delete lock;
}
EXTERN CHAIKRNL_FUNC cpu_status_t acquire_spinlock(spinlock_t lock)
{
	pspinlock slock = (pspinlock)lock;
	cpu_status_t v = arch_disable_interrupts();
	//We won't be preempted, so we're only contending with other CPUs
	while (!arch_cas(&slock->value, 0, 1)) { arch_pause(); }
	return v;
}
EXTERN CHAIKRNL_FUNC void release_spinlock(spinlock_t lock, cpu_status_t status)
{
	pspinlock slock = (pspinlock)lock;
	if (!arch_cas(&slock->value, 1, 0))
		slock->value = 0;		//Should never be necessary, but this prevents freezing due to memory corruption.
	arch_restore_state(status);
}