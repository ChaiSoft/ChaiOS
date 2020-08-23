#include <scheduler.h>
#include <multiprocessor.h>
#include <arch/cpu.h>
#include <redblack.h>
#include <linkedlist.h>
#include <spinlock.h>
#include <kstdio.h>
#include <liballoc.h>

enum THREAD_STATE {
	RUNNING,
	READY,
	BLOCKING,
	BLOCKED,
	TERMINATING,
	TERMINATED
};

static bool scheduler_ready = false;

typedef struct _thread_local {
	struct _thread_local* selfptr;
	size_t tls_size;
	uint32_t free_slot;
}TLSBLOCK,*PTLSBLOCK;

static PTLSBLOCK tls_block_factory()
{
	PTLSBLOCK block = new TLSBLOCK;
	block->selfptr = block;
	block->tls_size = 0;
	block->free_slot = -1;
	return block;
}

typedef struct _thread {
	context_t threadctxt;
	THREAD_STATE state;
	THREAD_TYPE threadtype;
	uint32_t cpu_id;
	stack_t kernel_stack;
	stack_t user_stack;
	linked_list_node<_thread*> listnode;
	HTHREAD handle;
	thread_proc proc;
	void* ctxt;
	spinlock_t thread_lock;
	void* timeout_event;
	PTLSBLOCK threadlocal;
	size_t priority;
}THREAD, *PTHREAD;

#define CURRENT_THREAD() \
((PTHREAD)(void*)pcpu_data.runningthread)

EXTERN CHAIKRNL_FUNC HTHREAD current_thread()
{
	if (!isscheduler())
		return (HTHREAD)1;
	return CURRENT_THREAD()->handle;
}

static linked_list_node < PTHREAD>& get_node(PTHREAD thread)
{
	return thread->listnode;
}

typedef RedBlackTree<HTHREAD, PTHREAD> thread_map;

static spinlock_t allthreads_lock;
static thread_map all_threads;

typedef LinkedList<PTHREAD> thread_list;

static spinlock_t ready_lock;
static thread_list ready;

static void ap_startup_routine(void* data)
{
	while (1)
		arch_halt();
}

static void idle_thread(void*)
{
	while (1)
		arch_halt();
}

static bool tap_callback(uint32_t apid)
{
	create_thread(&idle_thread, nullptr, THREAD_PRIORITY_IDLE, KERNEL_IDLE);
	ap_run_routine(apid, &ap_startup_routine, nullptr);
	return false;
}

static void(*the_eoi)() = nullptr;

//#define kprintf(...)

static const size_t quantum = 20;

void destroy_thread(HTHREAD thread)
{
	arch_enable_breakpoint(0);
	auto st = acquire_spinlock(allthreads_lock);
	arch_enable_breakpoint(1);
	PTHREAD pt = all_threads[thread];
	arch_enable_breakpoint(0);
	release_spinlock(allthreads_lock, st);
	arch_enable_breakpoint(1);
	st = acquire_spinlock(pt->thread_lock);
	auto oldstate = pt->state;
	pt->state = TERMINATING;
	release_spinlock(pt->thread_lock, st);
	if (oldstate == READY)
	{
		st = acquire_spinlock(ready_lock);
		ready.remove(pt);
		release_spinlock(ready_lock, st);
	}
}

struct timeout_event {
	uint64_t timeout;
	PTHREAD thread;
	linked_list_node<timeout_event*> listnode;
};

linked_list_node<timeout_event*>& timeout_nodef(timeout_event* e)
{
	return e->listnode;
}

LinkedList<timeout_event*> timeouts;
spinlock_t timeout_lock;

void scheduler_timer_tick()
{
	if (!scheduler_ready)
		return;
#if 1
	auto st = acquire_spinlock(timeout_lock);
	for (auto it = timeouts.begin(); it != timeouts.end(); ++it)
	{
		if (it->timeout <= arch_get_system_timer())
		{
			//Timeout signal
			PTHREAD thread = it->thread;
			auto st2 = acquire_spinlock(thread->thread_lock);
			thread->state = READY;
			thread->timeout_event = nullptr;
			release_spinlock(thread->thread_lock, st2);
			st2 = acquire_spinlock(ready_lock);
			ready.insert(thread);
			release_spinlock(ready_lock, st2);
			auto rem = *it;
			++it;
			timeouts.remove(rem);
		}
		else
			++it;
	}
	release_spinlock(timeout_lock, st);
#endif
}

void scheduler_schedule(uint64_t tick)
{
	if (!scheduler_ready)
		return;
	if (tick > 0 && tick % quantum != 0)
		return;
	uint32_t current_irql = pcpu_data.irql;
	//kprintf(u"SCHEDULING\b\b\b\b\b\b\b\b\b\b");
	auto cpustat = arch_disable_interrupts();
	PTHREAD thread = CURRENT_THREAD();
get_ready:
	//kprintf(u"READY LOCK\b\b\b\b\b\b\b\b\b\b");
	auto stat = acquire_spinlock(ready_lock);
	PTHREAD next = ready.pop();
	release_spinlock(ready_lock, stat);
	//kprintf(u"UNLOCKINGR\b\b\b\b\b\b\b\b\b\b");
	if (!next)
	{
		goto sched_end;
	}
	if (next->state != READY)
	{
		goto get_ready;
	}
#if 1
	if (thread != 0 && next->priority < thread->priority && thread->state == RUNNING)
	{
		//kprintf(u"READY SPIN\b\b\b\b\b\b\b\b\b\b");
		stat = acquire_spinlock(ready_lock);
		ready.insert(next);
		release_spinlock(ready_lock, stat);
		//kprintf(u"UNLOCKINGS\b\b\b\b\b\b\b\b\b\b");
		goto sched_end;
	}
#endif
	if (thread != 0)
	{
		if (save_context(thread->threadctxt) == 0)
		{
			//kprintf(u"MAIN SWITCH\b\b\b\b\b\b\b\b\b\b\b");
			stat = acquire_spinlock(ready_lock);
			next->state = RUNNING;
			pcpu_data.runningthread = next;
			//kprintf(u"LOCK THREAD\b\b\b\b\b\b\b\b\b\b\b");
			auto dstat = acquire_spinlock(thread->thread_lock);
			switch (thread->state)
			{
			case BLOCKING:
				thread->state = BLOCKED;
				break;
			case TERMINATING:
				thread->state = TERMINATED;
				break;
			case RUNNING:
				thread->state = READY;
				ready.insert(thread);
			}
			release_spinlock(thread->thread_lock, dstat);
			release_spinlock(ready_lock, stat);
			//kprintf(u"THREAD SWITCH: %x -> %x\n", thread->handle, next->handle);
			arch_write_tls_base(next->threadlocal, 0);
			arch_write_kstack(thread->kernel_stack);
			jump_context(next->threadctxt, 0);
		}
	}
	else
	{
		next->state = RUNNING;
		pcpu_data.runningthread = next;
		//kprintf(u"THREAD SWITCH: %x\n", next->handle);
		arch_write_tls_base(next->threadlocal, 0);
		arch_write_kstack(thread->kernel_stack);
		jump_context(next->threadctxt, 0);
	}
sched_end:
	arch_restore_state(cpustat);
	if (current_irql == IRQL_KERNEL && pcpu_data.irql == IRQL_INTERRUPT)
	{
		the_eoi();
	}
	else if (current_irql == IRQL_INTERRUPT && pcpu_data.irql == IRQL_KERNEL)
	{
	}
	pcpu_data.irql = current_irql;
}

#include <kernelinfo.h>
EXTERN PKERNEL_BOOT_INFO getBootInfo();

void scheduler_init(void(*eoi)())
{
	the_eoi = eoi;
	//Create thread database
	PTHREAD kthread = new THREAD;	//Initial kernel thread
	kthread->cpu_id = arch_current_processor_id();
	kthread->state = RUNNING;
	kthread->kernel_stack = getBootInfo()->bootstack;
	kthread->user_stack = nullptr;
	kthread->timeout_event = nullptr;
	kthread->handle = (HTHREAD)1;
	kthread->threadctxt = context_factory();
	kthread->thread_lock = create_spinlock();
	kthread->priority = THREAD_PRIORITY_NORMAL;
	kthread->threadtype = KERNEL_MAIN;
	kthread->threadlocal = tls_block_factory();
	all_threads[kthread->handle] = kthread;
	allthreads_lock = create_spinlock();
	//arch_set_breakpoint(allthreads_lock, 4, BREAKPOINT_WRITE);
	pcpu_data.runningthread = kthread;
	ready.init(&get_node);
	ready_lock = create_spinlock();
	timeout_lock = create_spinlock();
	timeouts.init(&timeout_nodef);
	scheduler_ready = true;
	create_thread(&idle_thread, nullptr, THREAD_PRIORITY_IDLE, KERNEL_IDLE);
	iterate_aps(&tap_callback);
}

uint8_t isscheduler()
{
	return scheduler_ready ? 1 : 0;
}

static void inital_thread_proc()
{
	if(pcpu_data.irql >= IRQL_INTERRUPT)
		the_eoi();
	arch_enable_interrupts();

	PTHREAD thread = CURRENT_THREAD();

	thread->proc(thread->ctxt);
	//Thread has finished
	destroy_thread(thread->handle);
	while (1);
}

stack_t getThreadStack(HTHREAD thread, uint8_t user)
{
	auto st = acquire_spinlock(allthreads_lock);
	PTHREAD pt = all_threads[thread];
	release_spinlock(allthreads_lock, st);

	if (user)
		return pt->user_stack;
	else
		return pt->kernel_stack;
}

EXTERN CHAIKRNL_FUNC HTHREAD create_thread(thread_proc proc, void* param, size_t priority, size_t type)
{
	PTHREAD thread = new THREAD;
	if (!thread)
		return nullptr;
	thread->state = READY;
	if (!(thread->threadctxt = context_factory()))
		return nullptr;
	if (!(thread->kernel_stack = arch_create_stack(0, 0)))
		return nullptr;
	if (!(thread->thread_lock = create_spinlock()))
		return nullptr;
	if (type >= THREAD_TYPE::USER_THREAD)
	{
		if (!(thread->user_stack = arch_create_stack(0, 1)))
			return nullptr;
	}
	else
		thread->user_stack = NULL;


	thread->timeout_event = nullptr;
	thread->handle = (HTHREAD)thread;
	thread->proc = proc;
	thread->ctxt = param;
	thread->priority = priority;
	thread->threadtype = (THREAD_TYPE)type;
	thread->threadlocal = tls_block_factory();
	thread->threadlocal->selfptr = thread->threadlocal;

	auto st = acquire_spinlock(allthreads_lock);
	all_threads[thread->handle] = thread;
	release_spinlock(allthreads_lock, st);

	//Now create the initial thread context
	arch_new_thread(thread->threadctxt, thread->kernel_stack, &inital_thread_proc);
	auto stat = acquire_spinlock(ready_lock);
	ready.insert(thread);
	release_spinlock(ready_lock, stat);
	return thread->handle;
}

EXTERN CHAIKRNL_FUNC void wake_thread(HTHREAD thread)
{
	auto st = acquire_spinlock(allthreads_lock);
	PTHREAD pt = all_threads[thread];
	release_spinlock(allthreads_lock, st);

	st = acquire_spinlock(pt->thread_lock);
	auto oldstate = pt->state;
	pt->state = READY;
	release_spinlock(pt->thread_lock, st);
	if (oldstate == BLOCKED)
	{
		st = acquire_spinlock(ready_lock);
		ready.insert(pt);
		release_spinlock(ready_lock, st);
	}
}

uint8_t scheduler_wait(size_t timeout, spinlock_t lock, sched_should_wait _should_wait, void* fparam, cpu_status_t* stat)
{
#if 1
	*stat = acquire_spinlock(lock);
	if (_should_wait(lock, fparam) == 0)
		return 1;
	else
		release_spinlock(lock, *stat);
#endif
	PTHREAD current = CURRENT_THREAD();
	//kprintf(u"Thread %x waiting, CPU %d\n", runningt, pcpu_data.cpuid);

	if (timeout != TIMEOUT_INFINITY)
	{
		timeout_event* tout = new timeout_event;
		tout->thread = current;
		tout->timeout = arch_get_system_timer() + timeout;
		current->timeout_event = tout;
		auto st1 = acquire_spinlock(timeout_lock);
		timeouts.insert(tout);
		release_spinlock(timeout_lock, st1);
	}
	current->state = BLOCKED;
	*stat = acquire_spinlock(lock);
	while (_should_wait(lock, fparam) != 0)
	{
		release_spinlock(lock, *stat);
		if (timeout != TIMEOUT_INFINITY && (current->timeout_event == nullptr))
			break;
		scheduler_schedule(0);
		*stat = acquire_spinlock(lock);
		current->state = BLOCKED;
	}
	current->state = RUNNING;
	//Woken up
	if (timeout != TIMEOUT_INFINITY)
	{
		auto st = acquire_spinlock(timeout_lock);
		if (current->timeout_event == nullptr)
		{
			release_spinlock(timeout_lock, st);
			return 0;
		}
		else
		{
			timeouts.remove((timeout_event*)current->timeout_event);
			release_spinlock(timeout_lock, st);
			current->timeout_event = nullptr;
			return 1;
		}
	}
	return 1;
}

static tls_data_t* get_tls_slot(PTLSBLOCK block, tls_slot_t slot)
{
	return raw_offset<tls_data_t*>(block, sizeof(TLSBLOCK) + slot * sizeof(tls_data_t));
}

EXTERN CHAIKRNL_FUNC tls_slot_t AllocateKernelTls()
{
	PTLSBLOCK tls = (PTLSBLOCK)arch_read_tls(0, 0, sizeof(tls_data_t)*8);
	if (tls->free_slot == -1)
	{
		static const int NEWSLOTS = 16;
		void* tlsn = krealloc(tls, sizeof(tls) + (tls->tls_size + NEWSLOTS) * sizeof(tls_data_t));
		if (tlsn != (void*)tls)
		{
			tls = (PTLSBLOCK)tlsn;
			tls->selfptr = tls;
			arch_write_tls_base(tls, 0);
		}
		auto freeblocks = raw_offset<tls_data_t*>(tls, sizeof(TLSBLOCK) + tls->tls_size * sizeof(tls_data_t));
		for (int i = 0; i < NEWSLOTS; ++i)
		{
			freeblocks[i] = (tls_data_t)(tls->tls_size + i + 1);
		}
		freeblocks[NEWSLOTS - 1] = -1;
		tls->free_slot = tls->tls_size;
		tls->tls_size += NEWSLOTS;
	}
	auto slot = tls->free_slot;
	tls->free_slot = *get_tls_slot(tls, slot);
	*get_tls_slot(tls, slot) = 0;
	return slot;
}
EXTERN CHAIKRNL_FUNC tls_data_t ReadKernelTls(tls_slot_t slot)
{
	PTLSBLOCK tls = (PTLSBLOCK)arch_read_tls(0, 0, sizeof(tls_data_t) * 8);
	if (slot >= tls->tls_size)
		return 0;
	return *get_tls_slot(tls, slot);
}

EXTERN CHAIKRNL_FUNC void WriteKernelTls(tls_slot_t slot, tls_data_t value)
{
	PTLSBLOCK tls = (PTLSBLOCK)arch_read_tls(0, 0, sizeof(tls_data_t) * 8);
	if (slot >= tls->tls_size)
		return;
	*get_tls_slot(tls, slot) = value;
}

EXTERN CHAIKRNL_FUNC void FreeKernelTls(tls_slot_t slot)
{
	PTLSBLOCK tls = (PTLSBLOCK)arch_read_tls(0, 0, sizeof(tls_data_t) * 8);
	if (slot >= tls->tls_size)
		return;
	*get_tls_slot(tls, slot) = tls->free_slot;
	tls->free_slot = slot;
}