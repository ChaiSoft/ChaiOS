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
}THREAD, *PTHREAD;

static linked_list_node < PTHREAD>& get_node(PTHREAD thread)
{
	return thread->listnode;
}

typedef RedBlackTree<HTHREAD, PTHREAD> thread_map;

thread_map all_threads;

typedef LinkedList<PTHREAD> thread_list;

spinlock_t ready_lock;
thread_list ready;

static void ap_startup_routine(void* data)
{
	pcpu_data.runningthread = 0;
	while (1);
}

static void idle_thread(void*)
{
	while (1)
		arch_halt();
}

static bool tap_callback(uint32_t apid)
{
	create_thread(&idle_thread, nullptr);
	ap_run_routine(apid, &ap_startup_routine, nullptr);
	return false;
}

static const size_t quantum = 20;

void destroy_thread(HTHREAD thread)
{
	PTHREAD pt = all_threads[thread];
	auto st = acquire_spinlock(pt->thread_lock);
	auto oldstate = pt->state;
	pt->state = TERMINATING;
	release_spinlock(pt->thread_lock, st);
	if (oldstate == READY)
	{
		auto st = acquire_spinlock(ready_lock);
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
			PTHREAD thread = all_threads[it->thread];
			auto st2 = acquire_spinlock(thread->thread_lock);
			thread->state = READY;
			thread->timeout_event = nullptr;
			release_spinlock(thread->thread_lock, st2);
			st2 = acquire_spinlock(ready_lock);
			ready.insert(thread);
			release_spinlock(ready_lock, st2);
		}
	}
	release_spinlock(timeout_lock, st);
}

void scheduler_schedule(uint64_t tick)
{
	if (!scheduler_ready)
		return;
	if (tick % quantum != 0)
		return;
	auto stat = acquire_spinlock(ready_lock);
	PTHREAD next = ready.pop();
	release_spinlock(ready_lock, stat);
	if (!next || next->state != READY)
		return;
	HTHREAD running = pcpu_data.runningthread;
	if (running != 0)
	{
		PTHREAD thread = all_threads[running];
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
	all_threads[kthread->handle] = kthread;
	pcpu_data.runningthread = kthread->handle;
	ready.init(&get_node);
	ready_lock = create_spinlock();
	timeout_lock = create_spinlock();
	timeouts.init(&timeout_nodef);
	scheduler_ready = true;
	create_thread(&idle_thread, nullptr);
	iterate_aps(&tap_callback);
}

uint8_t isscheduler()
{
	return scheduler_ready ? 1 : 0;
}

static void inital_thread_proc()
{
	the_eoi();
	PTHREAD thread = all_threads[pcpu_data.runningthread];
	thread->proc(thread->ctxt);
	//Thread has finished
	destroy_thread(thread->handle);
	while (1);
}

HTHREAD create_thread(thread_proc proc, void* param)
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
	all_threads[thread->handle] = thread;
	//Now create the initial thread context
	arch_new_thread(thread->threadctxt, thread->stack, &inital_thread_proc);
	auto stat = acquire_spinlock(ready_lock);
	ready.insert(thread);
	release_spinlock(ready_lock, stat);
	return thread->handle;
}

void initialize_wait_queue(void*& queue)
{
	auto que = new LinkedList<PTHREAD>;
	queue = que;
	que->init(&get_node);
}

void wake_one_waiting(void* queue)
{
	LinkedList<PTHREAD>* que = (LinkedList<PTHREAD>*)queue;
	PTHREAD thread = (PTHREAD)que->pop();
	if (!thread)
		return;
	auto st = acquire_spinlock(thread->thread_lock);
	thread->state = READY;
	release_spinlock(thread->thread_lock, st);
}

uint8_t scheduler_wait(void* queue, size_t timeout, spinlock_t lock, cpu_status_t locks)
{
	LinkedList<PTHREAD>* que = (LinkedList<PTHREAD>*)queue;
	PTHREAD current = all_threads[pcpu_data.runningthread];
	que->insert(current);
	release_spinlock(lock, locks);

	if (timeout != TIMEOUT_INFINITY)
	{
		timeout_event* tout = new timeout_event;
		tout->thread = pcpu_data.runningthread;
		tout->timeout = arch_get_system_timer() + timeout;
		current->timeout_event = tout;
		auto st = acquire_spinlock(timeout_lock);
		timeouts.insert(tout);
		release_spinlock(timeout_lock, st);
	}
	current->state = BLOCKING;
	scheduler_schedule(0);
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