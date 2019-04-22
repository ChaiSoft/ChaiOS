#ifndef CHAIOS_SCHEDULER_H
#define CHAIOS_SCHEDULER_H

#include <stdheaders.h>
#include <spinlock.h>

typedef void* HTHREAD;

void scheduler_init(void(*eoi)());
typedef void(*thread_proc)(void*);
HTHREAD create_thread(thread_proc proc, void* param);
void scheduler_schedule(uint64_t tick);
void scheduler_timer_tick();
uint8_t isscheduler();
void initialize_wait_queue(void*& queue);
void wake_one_waiting(void* queue);
#define TIMEOUT_INFINITY SIZE_MAX
uint8_t scheduler_wait(void* queue, size_t timeout, spinlock_t lock, cpu_status_t locks);

#endif
