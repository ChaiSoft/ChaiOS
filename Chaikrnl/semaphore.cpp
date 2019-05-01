#include <semaphore.h>
#include <spinlock.h>
#include <linkedlist.h>
#include <scheduler.h>
#include <kstdio.h>

#define TEST_BEHAVIOUR 0

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
	char16_t* semname;
};


EXTERN CHAIKRNL_FUNC semaphore_t create_semaphore(size_t count, char16_t* name)
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
#if 0
	if (isscheduler())
		kprintf(u"Signalling semaphore %s, %x\n", sem->semname, count);
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
				wake_thread(ent->waiting);
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
		queueent->waiting = pcpu_data.runningthread;
		sem->sem->wait_queue->insert(queueent);
	}
	return res;
}

EXTERN CHAIKRNL_FUNC uint8_t wait_semaphore(semaphore_t lock, size_t count, size_t timeout)
{
	semaphore* sem = (semaphore*)lock;
	cpu_status_t st;
#if TEST_BEHAVIOUR
	if (isscheduler())
	{
		//kprintf(u"Waiting on semaphore %s with value %x, %x\n", sem->semname, sem->value, count);
		sem_wait_data waitdat;
		waitdat.count = count;
		waitdat.sem = sem;
		if (scheduler_wait(timeout, sem->spinlock, &should_sleep_sem, &waitdat, &st) == 0)
		{
			return 0;	//Timeout
		}
		//kprintf(u"Semaphore %s stopped waiting\n", sem->semname);
	}
	else
	{
		st = acquire_spinlock(sem->spinlock);
	}
#else
	st = acquire_spinlock(sem->spinlock);
	bool candec = sem->value >= count;
	while (!candec)
	{
		release_spinlock(sem->spinlock, st);
		arch_pause();
		st = acquire_spinlock(sem->spinlock);
		candec = sem->value >= count;
	}
#endif
	sem->value -= count;
	release_spinlock(sem->spinlock, st);
	return 1;
}