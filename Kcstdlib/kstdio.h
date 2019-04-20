#ifndef CHAIOS_KCSTDLIB_KSTDIO_H
#define CHAIOS_KCSTDLIB_KSTDIO_H

#include "kcstdlib.h"
#include <vaargs.h>

typedef void (*chaios_stdio_puts_proc)(const char16_t*);
EXTERN KCSTDLIB_FUNC void set_stdio_puts(chaios_stdio_puts_proc putsp);
EXTERN KCSTDLIB_FUNC void kputs(const char16_t* str);
EXTERN KCSTDLIB_FUNC void kprintf(const char16_t* format, ...);
EXTERN KCSTDLIB_FUNC void kprintf_a(const char* format, ...);
EXTERN KCSTDLIB_FUNC void kvprintf_a(const char* format, va_list args);

#endif
