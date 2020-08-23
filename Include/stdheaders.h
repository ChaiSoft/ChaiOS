#ifndef CHAIOS_STDHEADERS_H
#define CHAIOS_STDHEADERS_H

//#define _VCRUNTIME_H
#include <stdint.h>
#include <vaargs.h>

#ifdef __cplusplus
#define EXTERN extern "C"
#else
#define EXTERN
#endif

typedef intptr_t ssize_t;
#define SSIZE_MAX INTPTR_MAX

//This pointer may be a user mode specified buffer. Don't trust it
#define __user
//Kernel mode pointer
#define __kernel

#ifdef __cplusplus
extern "C++" {
	template <class T, class U> intptr_t raw_diff(T* p1, U* p2)
	{
		return (intptr_t)p1 - (intptr_t)p2;
	}
	template <class T, class U> T raw_offset(U p1, const intptr_t offset)
	{
		return (T)((size_t)p1 + offset);
	}
	template <class T, class U> T mem_after(U* p1)
	{
		return (T)(&p1[1]);
	}
}
#else
#define RAW_OFFSET(type, x, offset) (type)((size_t)x + offset)
#endif

#define DIV_ROUND_UP(x, y) \
((x + y - 1) / y)

#endif