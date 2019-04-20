#ifndef CHAIOS_SCHEDULER_H
#define CHAIOS_SCHEDULER_H

#include <stdheaders.h>

typedef void* HTHREAD;

void scheduler_init();
typedef void(*thread_proc)(void*);
HTHREAD create_thread(thread_proc proc, void* param);
void scheduler_schedule(uint64_t tick);

#endif
