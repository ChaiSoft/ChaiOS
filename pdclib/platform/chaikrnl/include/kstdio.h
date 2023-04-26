#ifndef CHAIOS_KCSTDLIB_KSTDIO_H
#define CHAIOS_KCSTDLIB_KSTDIO_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef void (*chaios_stdio_puts_proc)(const char16_t* str, void* ConsoleHandle);
	typedef void* (*chaios_read_stdio_handle)();
	KCSTDLIB_FUNC void set_stdio_puts(chaios_stdio_puts_proc putsp, chaios_read_stdio_handle stdhandle);
	KCSTDLIB_FUNC void kputs(const char16_t* str);
	KCSTDLIB_FUNC void kprintf(const char16_t* format, ...);
	//EXTERN KCSTDLIB_FUNC void kvprintf(const char16_t* format, va_list args);
	KCSTDLIB_FUNC void kprintf_a(const char* format, ...);
	KCSTDLIB_FUNC void kvprintf_a(const char* format, va_list args);

	KCSTDLIB_FUNC void enable_screen_locking();

#ifdef __cplusplus
}
#endif

#endif
