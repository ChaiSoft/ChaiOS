#include <semaphore.h>
#include <spinlock.h>
#include <linkedlist.h>
#include <scheduler.h>
#include <kstdio.h>

struct semaphore {
	spinlock_t spinlock;
	size_t value;
	void* wait_queue;
};

EXTERN CHAIKRNL_FUNC semaphore_t create_semaphore(size_t count)
{
	semaphore* sem = new semaphore;
	if (!sem)
		return nullptr;
	sem->spinlock = create_spinlock();
	sem->value = count;
	if (!sem->spinlock)
		return nullptr;
	initialize_wait_queue(sem->wait_queue);
	return (semaphore_t)sem;
}
EXTERN CHAIKRNL_FUNC void delete_semaphore(semaphore_t lock)
{
	delete (semaphore*)lock;
}
EXTERN CHAIKRNL_FUNC void signal_semaphore(semaphore_t lock, size_t count)
{
	semaphore* sem = (semaphore*)lock;
	auto st = acquire_spinlock(sem->spinlock);
	sem->value += count;
	//See about the wait queue
	if (isscheduler())
	{
		for (size_t n = 0; n < count; ++n)
			wake_one_waiting(sem->wait_queue);
	}
	release_spinlock(sem->spinlock, st);
}
EXTERN CHAIKRNL_FUNC uint8_t wait_semaphore(semaphore_t lock, size_t count, size_t timeout)
{
	semaphore* sem = (semaphore*)lock;
	auto st = acquire_spinlock(sem->spinlock);
	if (isscheduler())
	{
		while (sem->value < count)
		{
			if (scheduler_wait(sem->wait_queue, timeout, sem->spinlock, st) == 0)
				return 0;	//Timeout
			st = acquire_spinlock(sem->spinlock);
		}
	}
	sem->value -= count;
	release_spinlock(sem->spinlock, st);
	return 1;
}