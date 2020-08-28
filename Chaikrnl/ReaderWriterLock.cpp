#include "ReadersWriterLock.h"
#include <arch/cpu.h>

typedef volatile struct _sharedlock {
	size_t writersTicket;
	size_t activeReaders;
	size_t nowServing;
}sharedlock, *psharedlock;

EXTERN CHAIKRNL_FUNC sharespinlock_t SharedSpinlockCreate()
{
	psharedlock lock = new sharedlock;
	lock->writersTicket = 0;
	lock->activeReaders = 0;
	lock->nowServing = 0;
	return (sharespinlock_t)lock;
}
EXTERN CHAIKRNL_FUNC void SharedSpinlockDelete(sharespinlock_t lock)
{
	delete (psharedlock)lock;
}
EXTERN CHAIKRNL_FUNC cpu_status_t SharedSpinlockAcquire(sharespinlock_t lock, BOOL exclusive)
{
	psharedlock slock = (psharedlock)lock;
	cpu_status_t v = -1;
	if (exclusive)
	{
		size_t ticket = slock->writersTicket;

		do
		{
			if (arch_cas(&slock->writersTicket, ticket, ticket + 1)) {
				break;
			}
			arch_pause();
			ticket = slock->writersTicket;
		} while (true);

		//We've got our ticket, wait to be served
		do
		{
			if (arch_cas(&slock->nowServing, ticket, ticket)) {
				break;
			}
			arch_pause();
		} while (true);

		//We are now being served
		//We want to WRITE the lock
		v = arch_disable_interrupts();
		do
		{
			if (arch_cas(&slock->activeReaders, 0, SIZE_MAX)) {
				break;
			}
			arch_pause();
		} while (true);
		//We now have prevented any readers from entering.
		return v;
	}
	else
	{
		//Don't preempt earlier writers
		size_t ticketVal = slock->writersTicket;
		do
		{
			if (slock->nowServing >= ticketVal)
				break;
			arch_pause();
		} while (true);

		//Now establish that we want to read
		size_t readers = slock->activeReaders;
		do
		{
			if (readers == SIZE_MAX)
			{
				//Writer is active
			}
			else if (arch_cas(&slock->activeReaders, readers, readers+1)) {
				//Taken lock
				break;
			}
			arch_pause();
			readers = slock->activeReaders;
		} while (true);
	}
	return v;
}
EXTERN CHAIKRNL_FUNC void SharedSpinlockRelease(sharespinlock_t lock, cpu_status_t status)
{
	psharedlock slock = (psharedlock)lock;
	if (status == -1)
	{
		//Was reader
		size_t readers = slock->activeReaders;
		do
		{
			if (arch_cas(&slock->activeReaders, readers, readers - 1)) {
				//Released lock
				break;
			}
			arch_pause();
			readers = slock->activeReaders;
		} while (true);
	}
	else
	{
		//No longer writing
		if (!arch_cas(&slock->activeReaders, SIZE_MAX, 0)) {
			//This really shouldn't happen. TODO: panic
		}
		//Let later writers and readers be served
		size_t ticket = slock->nowServing;
		do
		{
			if (arch_cas(&slock->nowServing, ticket, ticket + 1)) {
				break;
			}
			arch_pause();
			ticket = slock->nowServing;
		} while (true);

		arch_restore_state(status);
	}
}