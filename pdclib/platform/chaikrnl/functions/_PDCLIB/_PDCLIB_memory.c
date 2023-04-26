#include <stdint.h>

#define NULL 0

static void* (*allocate)(size_t) = NULL;
static int(*deallocate)(void*, size_t) = NULL;
void* sbrk(size_t size)
{
	return allocate(size);
}
int liballoc_free(void* ptr, size_t size)
{
	return deallocate(ptr, size);
}

void setLiballocAllocator(void* (*a)(size_t), int(*f)(void*, size_t))
{
	allocate = a;
	deallocate = f;
}