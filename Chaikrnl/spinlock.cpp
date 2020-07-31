#include <spinlock.h>
#include <arch/cpu.h>
#include <kstdio.h>

typedef struct _spinlock {
	size_t value;
}spinlock, *pspinlock;

static spinlock s_lock;

EXTERN CHAIKRNL_FUNC spinlock_t get_static_spinlock()
{
	return (spinlock_t)&s_lock;
}

static const int num_locks = 16;
static spinlock early_locks[num_locks];
static int offset = 0;

EXTERN CHAIKRNL_FUNC spinlock_t create_spinlock()
{
	pspinlock lock = new spinlock;
	if (!lock)
	{
		if (offset < num_locks)
		{
			early_locks[offset].value = 0;
			return &early_locks[offset++];
		}
		return nullptr;
	}
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
	if (slock->value > 1)
	{
		kprintf(u"SPINLOCK CORRUPTED: %x (value: %d)\n", slock, slock->value);
		slock->value = 0;
	}
	//We won't be preempted, so we're only contending with other CPUs
	do
	{
		if (arch_cas(&slock->value, 0, 1)) {
			break;
		}
		arch_pause();
		volatile size_t* lockd = &slock->value;
		while (*lockd == 1);
	} while (true);
	return v;
}
EXTERN CHAIKRNL_FUNC void release_spinlock(spinlock_t lock, cpu_status_t status)
{
	pspinlock slock = (pspinlock)lock;
	if (!arch_cas(&slock->value, 1, 0))
		slock->value = 0;		//Should never be necessary, but this prevents freezing due to memory corruption.
	arch_restore_state(status);
}