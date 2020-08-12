#ifndef CHAIOS_LWIP_ARCH_CC_H
#define CHAIOS_LWIP_ARCH_CC_H

#ifndef __cplusplus
typedef uint16_t char16_t;
#endif
#include <kstdio.h>

#define LWIP_PLATFORM_DIAG(x) kprintf_a x
#define LWIP_PLATFORM_ASSERT(x) kprintf_a(x "\n")
#define LWIP_NO_STDDEF_H 1
#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_LIMITS_H 0
#define LWIP_NO_CTYPE_H 1

#define X8_F "x"
#define U16_F "d"
#define S16_F "d"
#define X16_F "x"
#define U32_F "d"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "d"

#if 0
#define PACK_STRUCT_BEGIN _Pragma(pack(push, 1))
#define PACK_STRUCT_END _Pragma(pack(pop, 1))
#endif

#if 0
#define ENOMEM 1
#define ENOBUFS 1
#define EWOULDBLOCK 1
#define EHOSTUNREACH 1
#define EINPROGRESS 1
#define EINVAL 1
#define EADDRINUSE 1
#define EALREADY 1
#define EISCONN 1
#define ENOTCONN 1
#define ECONNABORTED 1
#define ECONNRESET 1
#define EIO 1
#endif

#endif