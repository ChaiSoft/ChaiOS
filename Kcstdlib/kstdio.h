#ifndef CHAIOS_KCSTDLIB_KSTDIO_H
#define CHAIOS_KCSTDLIB_KSTDIO_H

#include "kcstdlib.h"

typedef void (*chaios_stdio_puts_proc)(const char16_t*);
EXTERN KCSTDLIB_FUNC void set_stdio_puts(chaios_stdio_puts_proc putsp);
EXTERN KCSTDLIB_FUNC void kputs(const char16_t* str);
EXTERN KCSTDLIB_FUNC void kprintf(const char16_t* format, ...);

#endif
