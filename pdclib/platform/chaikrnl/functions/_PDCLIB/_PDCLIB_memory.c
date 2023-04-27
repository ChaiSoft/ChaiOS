#include <stdint.h>
#include <stdlib.h>
#include <liballoc.h>

//#define NULL 0
//
//static void* (*allocate)(size_t) = NULL;
//static int(*deallocate)(void*, size_t) = NULL;
//void* sbrk(size_t size)
//{
//	return allocate(size);
//}
//int liballoc_free(void* ptr, size_t size)
//{
//	return deallocate(ptr, size);
//}
//
//KCSTDLIB_FUNC void setLiballocAllocator(void* (*a)(size_t), int(*f)(void*, size_t))
//{
//	allocate = a;
//	deallocate = f;
//}

KCSTDLIB_FUNC void* malloc(size_t sz)
{
	return kmalloc(sz);
}
KCSTDLIB_FUNC void free(void* ptr)
{
	kfree(ptr);
}

KCSTDLIB_FUNC void* realloc(void* ptr, size_t sz)
{
	return krealloc(ptr, sz);
}

KCSTDLIB_FUNC void* calloc(size_t nmemb, size_t sz)
{
	return kcalloc(nmemb, sz);
}
