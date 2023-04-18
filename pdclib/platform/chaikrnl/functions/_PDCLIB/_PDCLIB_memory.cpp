#include <stdint.h>
#include <stdlib.h>
#include <kstdio.h>
#include <liballoc.h>
#include <pmmngr.h>

static void* (*allocate)(size_t) = NULL;
static int(*deallocate)(void*, size_t) = NULL;
static size_t PtrDequeue = INT64_MAX;
static size_t PtrEnqueue = NULL;


EXTERN void* sbrk(size_t size)
{
	//kprintf(u"Sbrk(%x), enq: %x, deq: %x", size, PtrEnqueue, PtrDequeue);
	size_t DesiredPos = PtrDequeue + size;
	void* Result = NULL;
	if (DesiredPos >= PtrEnqueue)
	{
		auto szpg = DIV_ROUND_UP(size, PAGESIZE);
		Result = allocate(szpg);
		if (!Result) return NULL;
		if (PtrEnqueue == (size_t)Result)
		{
			//Contiguous
			Result = (void*)PtrDequeue;
		}
		PtrEnqueue = raw_offset<size_t>(Result, szpg * PAGESIZE);
		PtrDequeue = raw_offset<size_t>(Result, size);

	}
	else
	{
		Result = (void*)PtrDequeue;
		PtrDequeue = DesiredPos;
	}
	//kprintf(u" -> enq: %x, deq: %x, ret %x\n", PtrEnqueue, PtrDequeue, Result);
	return Result;
}
//EXTERN int liballoc_free(void* ptr, size_t size)
//{
//	//return deallocate(ptr, size);
//}
//
//EXTERN KCSTDLIB_FUNC void setLiballocAllocator(void* (*a)(size_t), int(*f)(void*, size_t))
//{
//	allocate = a;
//	deallocate = f;
//}

EXTERN void* malloc(size_t sz)
{
	return kmalloc(sz);
}
EXTERN KCSTDLIB_FUNC void free(void* ptr)
{
	free(ptr);
}


//EXTERN KCSTDLIB_FUNC void* kmalloc(size_t sz)
//{
//	void* result = malloc(sz);
//	if (!result)
//	{
//		kputs(u"\n\nKernel Panic: NULL malloc\n");
//		while (1);
//	}
//	return result;
//}
//EXTERN KCSTDLIB_FUNC void kfree(void* ptr)
//{
//	free(ptr);
//}
//
//EXTERN KCSTDLIB_FUNC void* krealloc(void* ptr, size_t sz)
//{
//	return realloc(ptr, sz);
//}
//
//EXTERN KCSTDLIB_FUNC void* kcalloc(size_t nmemb, size_t sz)
//{
//	return calloc(nmemb, sz);
//}
