#include <scheduler.h>
#include <multiprocessor.h>
#include <arch/cpu.h>
#include <redblack.h>
#include <linkedlist.h>
#include <spinlock.h>

enum THREAD_STATE {
	RUNNING,
	READY,
	BLOCKED
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

static bool tap_callback(uint32_t apid)
{
	ap_run_routine(apid, &ap_startup_routine, nullptr);
	return false;
}

static const size_t quantum = 20;

void scheduler_schedule(uint64_t tick)
{
	if (!scheduler_ready)
		return;
	if (tick % quantum != 0)
		return;
	auto stat = acquire_spinlock(ready_lock);
	PTHREAD next = ready.pop();
	release_spinlock(ready_lock, stat);
	if (!next)
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
			thread->state = READY;
			ready.insert(thread);
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

void scheduler_init()
{
	//Create thread database
	PTHREAD kthread = new THREAD;	//Initial kernel thread
	kthread->cpu_id = arch_current_processor_id();
	kthread->state = RUNNING;
	kthread->stack = nullptr;
	kthread->handle = (HTHREAD)1;
	kthread->threadctxt = context_factory();
	all_threads[kthread->handle] = kthread;
	pcpu_data.runningthread = kthread->handle;
	ready.init(&get_node);
	ready_lock = create_spinlock();
	scheduler_ready = true;
	iterate_aps(&tap_callback);
}

static void inital_thread_proc()
{
	PTHREAD thread = all_threads[pcpu_data.runningthread];
	thread->proc(thread->ctxt);
	while (1);
}

HTHREAD create_thread(thread_proc proc, void* param)
{
	PTHREAD thread = new THREAD;
	thread->state = RUNNING;
	thread->threadctxt = context_factory();
	thread->stack = arch_create_kernel_stack();
	thread->handle = (HTHREAD)thread;
	thread->proc = proc;
	thread->ctxt = param;
	all_threads[thread->handle] = thread;
	//Now create the initial thread context
	arch_new_thread(thread->threadctxt, thread->stack, &inital_thread_proc);
	//Swap threads to avoid interrupt issues
	PTHREAD curr = all_threads[pcpu_data.runningthread];
	curr->state = READY;
	auto stat = acquire_spinlock(ready_lock);
	ready.insert(curr);
	if (save_context(curr->threadctxt) == 0)
	{
		pcpu_data.runningthread = thread->handle;
		release_spinlock(ready_lock, stat);
		jump_context(thread->threadctxt, 0);
	}
	release_spinlock(ready_lock, stat);

	return thread->handle;
}