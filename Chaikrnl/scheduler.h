#ifndef CHAIOS_SCHEDULER_H
#define CHAIOS_SCHEDULER_H

#include <stdheaders.h>
#include <spinlock.h>

typedef void* HTHREAD;

#define THREAD_PRIORITY_IDLE 0
#define THREAD_PRIORITY_NORMAL 16

void scheduler_init(void(*eoi)());
typedef void(*thread_proc)(void*);
HTHREAD create_thread(thread_proc proc, void* param, size_t priority = THREAD_PRIORITY_NORMAL);
void scheduler_schedule(uint64_t tick);
void scheduler_timer_tick();
uint8_t isscheduler();
void wake_thread(HTHREAD thread);
#define TIMEOUT_INFINITY SIZE_MAX
typedef uint8_t(*sched_should_wait)(spinlock_t lock, void* param);
uint8_t scheduler_wait(size_t timeout, spinlock_t lock, sched_should_wait _should_wait, void* fparam, cpu_status_t* st);

#endif
