#ifndef CHAIOS_SCHEDULER_H
#define CHAIOS_SCHEDULER_H

#include <stdheaders.h>
#include <spinlock.h>

typedef void* HTHREAD;

#define THREAD_PRIORITY_IDLE 0
#define THREAD_PRIORITY_NORMAL 16

void scheduler_init(void(*eoi)());
typedef void(*thread_proc)(void*);

#ifdef __cplusplus
enum THREAD_TYPE {
	KERNEL_MAIN,
	KERNEL_TASK,
	KERNEL_IDLE,
	DRIVER_EVENT,
	DRIVER_TASK,
	USER_THREAD
};
EXTERN CHAIKRNL_FUNC HTHREAD create_thread(thread_proc proc, void* param, size_t priority = THREAD_PRIORITY_NORMAL, size_t type = KERNEL_TASK);
#else
#define KERNEL_TASK 1
EXTERN CHAIKRNL_FUNC HTHREAD create_thread(thread_proc proc, void* param, size_t priority, size_t type);
#endif

void scheduler_schedule(uint64_t tick);
void scheduler_timer_tick();
uint8_t isscheduler();
EXTERN CHAIKRNL_FUNC void wake_thread(HTHREAD thread);
EXTERN CHAIKRNL_FUNC HTHREAD current_thread();
#define TIMEOUT_INFINITY SIZE_MAX
typedef uint8_t(*sched_should_wait)(spinlock_t lock, void* param);
uint8_t scheduler_wait(size_t timeout, spinlock_t lock, sched_should_wait _should_wait, void* fparam, cpu_status_t* st);



typedef uint64_t tls_data_t;
typedef uint32_t tls_slot_t;
EXTERN CHAIKRNL_FUNC tls_slot_t AllocateKernelTls();
EXTERN CHAIKRNL_FUNC tls_data_t ReadKernelTls(tls_slot_t slot);
EXTERN CHAIKRNL_FUNC void WriteKernelTls(tls_slot_t slot, tls_data_t value);
EXTERN CHAIKRNL_FUNC void FreeKernelTls(tls_slot_t slot);

#endif
