#ifndef CHAIOS_EARLY_GRAPHICS_H
#define CHAIOS_EARLY_GRAPHICS_H

#include <stdheaders.h>
#include <kernelinfo.h>

typedef uint32_t COLORREF;
#define RGB(r, g ,b) \
	((r & 0xFF) | ((g << 8)&0xFF00) | ((b << 16)&0xFF0000))

#define RED(col) \
	(col & 0xFF)

#define GREEN(col) \
	((col>>8) & 0xFF)

#define BLUE(col) \
	((col>>16) & 0xFF)

void InitialiseGraphics(const FRAMEBUFFER_INFORMATION& info);
void gputs_k(const char16_t* str);
void populate_kterm_info(void*& inf);
typedef void*(*memcpy_proc)(void*, const void*, size_t);
void set_memcpy(memcpy_proc proc);
void set_scrolllines(int lines);



#endif