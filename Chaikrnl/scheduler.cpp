#include <scheduler.h>
#include <multiprocessor.h>
#include <arch/cpu.h>
#include <redblack.h>
#include <linkedlist.h>
#include <spinlock.h>
#include <kstdio.h>

enum THREAD_STATE {
	RUNNING,
	READY,
	BLOCKING,
	BLOCKED,
	TERMINATING,
	TERMINATED
};

static bool scheduler_ready = false;

typedef struct _thread {
	context_t threadctxt;
	THREAD_STATE state;
	uint32_t cpu_id;
	kstack_t stack;
	linked_list_node<_thread*> listnode;
	HTHREAD handle;
	thread_proc proc;
	void* ctxt;
	spinlock_t thread_lock;
	void* timeout_event;
	size_t priority;
}THREAD, *PTHREAD;

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
	create_thread(&idle_thread, nullptr, THREAD_PRIORITY_IDLE);
	ap_run_routine(apid, &ap_startup_routine, nullptr);
	return false;
}

static const size_t quantum = 20;

void destroy_thread(HTHREAD thread)
{
	auto st = acquire_spinlock(allthreads_lock);
	PTHREAD pt = all_threads[thread];
	release_spinlock(allthreads_lock, st);
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
	HTHREAD thread;
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
	auto st = acquire_spinlock(timeout_lock);
	for (auto it = timeouts.begin(); it != timeouts.end(); ++it)
	{
		if (it->timeout <= arch_get_system_timer())
		{
			//Timeout signal
			auto st2 = acquire_spinlock(allthreads_lock);
			PTHREAD thread = all_threads[it->thread];
			release_spinlock(allthreads_lock, st2);
			st2 = acquire_spinlock(thread->thread_lock);
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
}

void scheduler_schedule(uint64_t tick)
{
	if (!scheduler_ready)
		return;
	if (tick % quantum != 0)
		return;
	auto cpustat = arch_disable_interrupts();
	HTHREAD running = pcpu_data.runningthread;
	PTHREAD thread = nullptr;
	if (running != 0)
	{
		auto st = acquire_spinlock(allthreads_lock);
		thread = all_threads[running];
		release_spinlock(allthreads_lock, st);
	}
get_ready:
	auto stat = acquire_spinlock(ready_lock);
	PTHREAD next = ready.pop();
	release_spinlock(ready_lock, stat);
	if (!next)
	{
		arch_restore_state(cpustat);
		return;
	}
	if (next->state != READY)
	{
		goto get_ready;
	}
#if 1
	if (running != 0 && next->priority < thread->priority && thread->state == RUNNING)
	{
		stat = acquire_spinlock(ready_lock);
		ready.insert(next);
		release_spinlock(ready_lock, stat);
		arch_restore_state(cpustat);
		return;
	}
#endif
	if (running != 0)
	{
		if (save_context(thread->threadctxt) == 0)
		{
			stat = acquire_spinlock(ready_lock);
			next->state = RUNNING;
			pcpu_data.runningthread = next->handle;
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
			jump_context(next->threadctxt, 0);
		}
	}
	else
	{
		next->state = RUNNING;
		pcpu_data.runningthread = next->handle;
		jump_context(next->threadctxt, 0);
	}
	arch_restore_state(cpustat);
}

static void(*the_eoi)() = nullptr;

void scheduler_init(void(*eoi)())
{
	the_eoi = eoi;
	//Create thread database
	PTHREAD kthread = new THREAD;	//Initial kernel thread
	kthread->cpu_id = arch_current_processor_id();
	kthread->state = RUNNING;
	kthread->stack = nullptr;
	kthread->timeout_event = nullptr;
	kthread->handle = (HTHREAD)1;
	kthread->threadctxt = context_factory();
	kthread->thread_lock = create_spinlock();
	kthread->priority = THREAD_PRIORITY_NORMAL;
	all_threads[kthread->handle] = kthread;
	allthreads_lock = create_spinlock();
	pcpu_data.runningthread = kthread->handle;
	ready.init(&get_node);
	ready_lock = create_spinlock();
	timeout_lock = create_spinlock();
	timeouts.init(&timeout_nodef);
	scheduler_ready = true;
	create_thread(&idle_thread, nullptr, THREAD_PRIORITY_IDLE);
	iterate_aps(&tap_callback);
}

uint8_t isscheduler()
{
	return scheduler_ready ? 1 : 0;
}

static void inital_thread_proc()
{
	the_eoi();
	auto st = acquire_spinlock(allthreads_lock);
	PTHREAD thread = all_threads[pcpu_data.runningthread];
	release_spinlock(allthreads_lock, st);
	thread->proc(thread->ctxt);
	//Thread has finished
	destroy_thread(thread->handle);
	while (1);
}

HTHREAD create_thread(thread_proc proc, void* param, size_t priority)
{
	PTHREAD thread = new THREAD;
	if (!thread)
		return nullptr;
	thread->state = READY;
	if (!(thread->threadctxt = context_factory()))
		return nullptr;
	if (!(thread->stack = arch_create_kernel_stack()))
		return nullptr;
	if (!(thread->thread_lock = create_spinlock()))
		return nullptr;
	thread->timeout_event = nullptr;
	thread->handle = (HTHREAD)thread;
	thread->proc = proc;
	thread->ctxt = param;
	thread->priority = priority;
	auto st = acquire_spinlock(allthreads_lock);
	all_threads[thread->handle] = thread;
	release_spinlock(allthreads_lock, st);
	//Now create the initial thread context
	arch_new_thread(thread->threadctxt, thread->stack, &inital_thread_proc);
	auto stat = acquire_spinlock(ready_lock);
	ready.insert(thread);
	release_spinlock(ready_lock, stat);
	return thread->handle;
}

void wake_thread(HTHREAD thread)
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
	HTHREAD runningt = pcpu_data.runningthread;
	//kprintf(u"Thread %x waiting, CPU %d\n", runningt, pcpu_data.cpuid);
	PTHREAD current = all_threads[runningt];

	if (timeout != TIMEOUT_INFINITY)
	{
		timeout_event* tout = new timeout_event;
		tout->thread = runningt;
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
		if (timeout != TIMEOUT_INFINITY && current->timeout_event == nullptr)
			break;
		scheduler_schedule(0);
		current->state = BLOCKED;
		*stat = acquire_spinlock(lock);
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