#include <semaphore.h>
#include <spinlock.h>
#include <linkedlist.h>
#include <scheduler.h>
#include <kstdio.h>

#define kprintf(...)
#define TEST_BEHAVIOUR 1

struct wait_queue_entry {
	HTHREAD waiting;
	linked_list_node<wait_queue_entry*> listnode;
};

static linked_list_node<wait_queue_entry*>& get_wait_node(wait_queue_entry* ent)
{
	return ent->listnode;
}

struct semaphore {
	spinlock_t spinlock;
	size_t value;
	LinkedList<wait_queue_entry*>* wait_queue;
	const char16_t* semname;
};


EXTERN CHAIKRNL_FUNC semaphore_t create_semaphore(size_t count, const char16_t* name)
{
	semaphore* sem = new semaphore;
	if (!sem)
		return nullptr;
	sem->spinlock = create_spinlock();
	sem->value = count;
	sem->semname = name;
	if (!sem->spinlock)
		return nullptr;
	sem->wait_queue = new LinkedList<wait_queue_entry*>(&get_wait_node);
	return (semaphore_t)sem;
}
EXTERN CHAIKRNL_FUNC void delete_semaphore(semaphore_t lock)
{
	delete (semaphore*)lock;
}
EXTERN CHAIKRNL_FUNC void signal_semaphore(semaphore_t lock, size_t count)
{
	semaphore* sem = (semaphore*)lock;
#if 1
	if (isscheduler())
		kprintf(u"Signalling semaphore %s, %x (spinlock: %x)\n", sem->semname, count, *(size_t*)sem->spinlock);
#endif
	auto st = acquire_spinlock(sem->spinlock);
	sem->value += count;
	//See about the wait queue
#if TEST_BEHAVIOUR
	if (isscheduler())
	{
		for (size_t n = 0; n < count; ++n)
		{
			wait_queue_entry* ent = sem->wait_queue->pop();
			if (ent)
			{
				//kprintf(u"Thread %x waking\n", ent->waiting);
				release_spinlock(sem->spinlock, st);
				wake_thread(ent->waiting);
				return;
			}
		}
	}
#endif
	release_spinlock(sem->spinlock, st);
}

struct sem_wait_data {
	semaphore* sem;
	size_t count;
};

static uint8_t should_sleep_sem(spinlock_t lock, void* param)
{
	sem_wait_data* sem = (sem_wait_data*)param;
	uint8_t res = sem->sem->value < sem->count ? 1 : 0;
	if (res == 1)
	{
		wait_queue_entry* queueent = new wait_queue_entry;
		queueent->waiting = current_thread();
		sem->sem->wait_queue->insert(queueent);
	}
	return res;
}

static auto get_debug_flags()
{
	auto flags = arch_disable_interrupts();
	arch_restore_state(flags);
	return flags;
}

CHAIKRNL_FUNC void write_semaphore(semaphore_t lock, size_t count)
{
	semaphore* sem = (semaphore*)lock;
	auto st = acquire_spinlock(sem->spinlock);
	sem->value = count;
	release_spinlock(sem->spinlock, st);
}

CHAIKRNL_FUNC size_t peek_semaphore(semaphore_t lock)
{
	semaphore* sem = (semaphore*)lock;
	return sem->value;
}

EXTERN CHAIKRNL_FUNC uint8_t wait_semaphore(semaphore_t lock, size_t count, size_t timeout)
{
	semaphore* sem = (semaphore*)lock;
	cpu_status_t st;
#if 0
	if (isscheduler())
		kprintf(u"Waiting semaphore %s, %x, %dms\n", sem->semname, count, timeout);
#endif

#if TEST_BEHAVIOUR
	if (isscheduler())
	{
		kprintf(u"Waiting on semaphore %s with value %x (spinlock: %x), %x\n", sem->semname, *(size_t*)sem->spinlock, sem->value, count);
		sem_wait_data waitdat;
		waitdat.count = count;
		waitdat.sem = sem;
		if (scheduler_wait(timeout, sem->spinlock, &should_sleep_sem, &waitdat, &st) == 0)
		{
			release_spinlock(sem->spinlock, st);
			return 0;	//Timeout
		}
		kprintf(u"Semaphore %s stopped waiting\n", sem->semname);
	}
	else
	{
		st = acquire_spinlock(sem->spinlock);
	}
#else
	auto time = arch_get_system_timer();
	int live = 0;
	while (1)
	{
		while (sem->value < count)
		{
			if (false) /*isscheduler()*/
			{
				uint64_t timer = arch_get_system_timer();
				kprintf(u"%d, IF%d, %d", timer, (get_debug_flags() >> 9) & 1, live);
				live = (live + 1) % 2;
				kputs(u"\b\b\b\b\b\b\b\b");
				if (timer == 0)
					kputs(u"\b");
				for (; timer != 0; timer /= 10)
				{
					kputs(u"\b");
				}
			}
			if (timeout != TIMEOUT_INFINITY && arch_get_system_timer() > time + timeout)
				return 0;
			arch_pause();
		}
		st = acquire_spinlock(sem->spinlock);
		if (sem->value >= count)
		{
			break;
		}
		release_spinlock(sem->spinlock, st);

	}
#endif
	sem->value -= count;
	release_spinlock(sem->spinlock, st);
	return 1;
}