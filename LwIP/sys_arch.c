/*
 * Copyright (c) 2017 Simon Goldschmidt
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Simon Goldschmidt
 *
 */


#include <lwip/opt.h>
#include <lwip/arch.h>
#if !NO_SYS
#include <arch/sys_arch.h>
#endif
#include <lwip/stats.h>
#include <lwip/debug.h>
#include <lwip/sys.h>

#include <string.h>
#include <liballoc.h>
#include <semaphore.h>

u32_t
sys_jiffies(void)
{
	return (u32_t)arch_get_system_timer();
}

u32_t
sys_now(void)
{
	return (u32_t)arch_get_system_timer();
}

void
sys_init(void)
{
}

static const char* chars = "0123456789ABCDEF";
int atoi(const char* str)
{
	size_t value = 0;
	int negative = 0;
	if (*str == '-')
	{
		negative = 1;
		++str;
	}
	while (*str)
	{
		value *= 10;
		size_t i = 0;
		for (; i < 16; ++i)
			if (chars[i] == *str) break;
		if (i == 16)
			return 0;
		value += i;
		++str;
	}
	if (negative)
		return -value;
	else
		return value;
}

#pragma function(memmove)
void * memmove(void* dest, const void* src, unsigned int n)
{
	char *pDest = (char *)dest;
	const char *pSrc = (const char*)src;
	//allocate memory for tmp array
	char *tmp = (char *)kmalloc(sizeof(char) * n);
	if (NULL == tmp)
	{
		return NULL;
	}
	else
	{
		unsigned int i = 0;
		// copy src to tmp array
		for (i = 0; i < n; ++i)
		{
			*(tmp + i) = *(pSrc + i);
		}
		//copy tmp to dest
		for (i = 0; i < n; ++i)
		{
			*(pDest + i) = *(tmp + i);
		}
		kfree(tmp); //free allocated memory
	}
	return dest;
}

#if !NO_SYS

test_sys_arch_waiting_fn the_waiting_fn;

void
test_sys_arch_wait_callback(test_sys_arch_waiting_fn waiting_fn)
{
  the_waiting_fn = waiting_fn;
}

uint16_t nullstr = 0;

err_t
sys_sem_new(sys_sem_t *sem, u8_t count)
{
  LWIP_ASSERT("sem != NULL", sem != NULL);
  *sem = create_semaphore(count, &nullstr);
  return ERR_OK;
}

void
sys_sem_free(sys_sem_t *sem)
{
  LWIP_ASSERT("sem != NULL", sem != NULL);
  delete_semaphore(*sem);
  *sem = 0;
}

void
sys_sem_set_invalid(sys_sem_t *sem)
{
  LWIP_ASSERT("sem != NULL", sem != NULL);
  write_semaphore(*sem, -1);
}

int sys_sem_valid(sys_sem_t *sem)
{
	return (peek_semaphore(*sem) != -1);
}

/* semaphores are 1-based because RAM is initialized as 0, which would be valid */
u32_t
sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
	if (timeout == 0)
		timeout = TIMEOUT_INFINITY;
	if (wait_semaphore(*sem, 1, timeout))
		return SYS_ARCH_TIMEOUT;
  /* return the time we waited for the sem */
  return 1;
}

void
sys_sem_signal(sys_sem_t *sem)
{
  LWIP_ASSERT("sem != NULL", sem != NULL);
  signal_semaphore(*sem, 1);
}

err_t
sys_mutex_new(sys_mutex_t *mutex)
{
  LWIP_ASSERT("mutex != NULL", mutex != NULL);
  *mutex = sys_sem_new(mutex, 1);
  return ERR_OK;
}

void
sys_mutex_free(sys_mutex_t *mutex)
{
  /* parameter check */
  LWIP_ASSERT("mutex != NULL", mutex != NULL);
  LWIP_ASSERT("*mutex >= 1", *mutex >= 1);
  sys_sem_free(mutex);
}

void
sys_mutex_set_invalid(sys_mutex_t *mutex)
{
  LWIP_ASSERT("mutex != NULL", mutex != NULL);
  sys_sem_set_invalid(mutex);
}

int
sys_mutex_valid(sys_mutex_t *mutex)
{
	LWIP_ASSERT("mutex != NULL", mutex != NULL);
	return sys_sem_valid(mutex);
}

void
sys_mutex_lock(sys_mutex_t *mutex)
{
  /* nothing to do, no multithreading supported */
  LWIP_ASSERT("mutex != NULL", mutex != NULL);
  /* check that the mutext is valid and unlocked (no nested locking) */
  sys_arch_sem_wait(mutex, 0);
}

void
sys_mutex_unlock(sys_mutex_t *mutex)
{
  /* nothing to do, no multithreading supported */
  LWIP_ASSERT("mutex != NULL", mutex != NULL);
  sys_sem_signal(mutex);
}

#include <scheduler.h>

sys_thread_t
sys_thread_new(const char *name, lwip_thread_fn function, void *arg, int stacksize, int prio)
{
  LWIP_UNUSED_ARG(name);
  LWIP_UNUSED_ARG(function);
  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(stacksize);
  LWIP_UNUSED_ARG(prio);
  return create_thread(function, arg, prio, KERNEL_TASK);
}

err_t
sys_mbox_new(sys_mbox_t *mbox, int size)
{
  int mboxsize = size;
  LWIP_ASSERT("mbox != NULL", mbox != NULL);
  LWIP_ASSERT("size >= 0", size >= 0);
  if (size == 0) {
    mboxsize = 1024;
  }
  mbox->head = mbox->tail = 0;
  sys_sem_new(&mbox->sem, 0);
  mbox->lock = create_spinlock();
  mbox->q_mem = (void**)kmalloc(sizeof(void*)*mboxsize);
  mbox->size = mboxsize;
  mbox->used = 0;

  memset(mbox->q_mem, 0, sizeof(void*)*mboxsize);
  return ERR_OK;
}

void
sys_mbox_free(sys_mbox_t *mbox)
{
  /* parameter check */
  LWIP_ASSERT("mbox != NULL", mbox != NULL);
  LWIP_ASSERT("mbox->sem != NULL", mbox->sem != NULL);
  LWIP_ASSERT("mbox->sem == mbox", mbox->sem == mbox);
  LWIP_ASSERT("mbox->q_mem != NULL", mbox->q_mem != NULL);
  sys_sem_free(&mbox->sem);
  delete_spinlock(mbox->lock);
  kfree(mbox->q_mem);
  mbox->q_mem = NULL;
}

void
sys_mbox_set_invalid(sys_mbox_t *mbox)
{
  LWIP_ASSERT("mbox != NULL", mbox != NULL);
  LWIP_ASSERT("mbox->q_mem == NULL", mbox->q_mem == NULL);
  mbox->q_mem = NULL;
  mbox->head = -1;
}

void
sys_mbox_post(sys_mbox_t *q, void *msg)
{
  LWIP_ASSERT("q != SYS_MBOX_NULL", q != SYS_MBOX_NULL);
  LWIP_ASSERT("q->q_mem != NULL", q->q_mem != NULL);
  LWIP_ASSERT("q->used >= 0", q->used >= 0);
  LWIP_ASSERT("q->size > 0", q->size > 0);

  LWIP_ASSERT("mbox already full", q->used < q->size);
  cpu_status_t st = acquire_spinlock(q->lock);
  q->q_mem[q->head] = msg;
  q->head++;
  if (q->head >= (unsigned int)q->size) {
    q->head = 0;
  }
  LWIP_ASSERT("mbox is full!", q->head != q->tail);
  q->used++;
  release_spinlock(q->lock, st);
  signal_semaphore(q->sem, 1);
}

err_t
sys_mbox_trypost(sys_mbox_t *q, void *msg)
{
  LWIP_ASSERT("q != SYS_MBOX_NULL", q != SYS_MBOX_NULL);
  //LWIP_ASSERT("q->sem == q", q->sem == q);
  LWIP_ASSERT("q->q_mem != NULL", q->q_mem != NULL);
  LWIP_ASSERT("q->used >= 0", q->used >= 0);
  LWIP_ASSERT("q->size > 0", q->size > 0);
  LWIP_ASSERT("q->used <= q->size", q->used <= q->size);

  if (q->used == q->size) {
    return ERR_MEM;
  }
  sys_mbox_post(q, msg);
  return ERR_OK;
}

err_t
sys_mbox_trypost_fromisr(sys_mbox_t *q, void *msg)
{
  return sys_mbox_trypost(q, msg);
}

u32_t
sys_arch_mbox_fetch(sys_mbox_t *q, void **msg, u32_t timeout)
{
  u32_t ret = 0;
  u32_t ret2;
  LWIP_ASSERT("q != SYS_MBOX_NULL", q != SYS_MBOX_NULL);
  LWIP_ASSERT("q->q_mem != NULL", q->q_mem != NULL);
  LWIP_ASSERT("q->used >= 0", q->used >= 0);
  LWIP_ASSERT("q->size > 0", q->size > 0);

  if (sys_arch_sem_wait(&q->sem, timeout) == 0)
	  return SYS_ARCH_TIMEOUT;
  cpu_status_t st = acquire_spinlock(q->lock);
  if (q->used == 0) {
	  release_spinlock(q->lock, st);
	  return SYS_ARCH_TIMEOUT;
  }
  
  if (msg) {
	  *msg = q->q_mem[q->tail];
  }

  q->tail++;
  if (q->tail >= (unsigned int)q->size) {
	  q->tail = 0;
  }
  q->used--;

  release_spinlock(q->lock, st);
  
  return 0;
}

u32_t
sys_arch_mbox_tryfetch(sys_mbox_t *q, void **msg)
{
	cpu_status_t st = acquire_spinlock(q->lock);
  LWIP_ASSERT("q != SYS_MBOX_NULL", q != SYS_MBOX_NULL);
  LWIP_ASSERT("q->q_mem != NULL", q->q_mem != NULL);
  LWIP_ASSERT("q->used >= 0", q->used >= 0);
  LWIP_ASSERT("q->size > 0", q->size > 0);

  if (!q->used) {
	  release_spinlock(q->lock, st);
    return SYS_ARCH_TIMEOUT;
  }
  if(msg) {
    *msg = q->q_mem[q->tail];
  }

  q->tail++;
  if (q->tail >= (unsigned int)q->size) {
    q->tail = 0;
  }
  q->used--;
  LWIP_ASSERT("q->used >= 0", q->used >= 0);
  release_spinlock(q->lock, st);
  return 0;
}

int errno = 0;

#if LWIP_NETCONN_SEM_PER_THREAD
#error LWIP_NETCONN_SEM_PER_THREAD==1 not supported
#endif /* LWIP_NETCONN_SEM_PER_THREAD */

#endif /* !NO_SYS */
