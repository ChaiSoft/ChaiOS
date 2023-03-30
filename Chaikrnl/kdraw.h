#ifndef CHAIOS_EARLY_GRAPHICS_H
#define CHAIOS_EARLY_GRAPHICS_H

#include <stdheaders.h>
#include <kernelinfo.h>
#include <chaikrnl.h>

typedef uint32_t COLORREF;
#define RGB(r, g ,b) \
	((r & 0xFF) | ((g << 8)&0xFF00) | ((b << 16)&0xFF0000))

#define RED(col) \
	(col & 0xFF)

#define GREEN(col) \
	((col>>8) & 0xFF)

#define BLUE(col) \
	((col>>16) & 0xFF)

void InitialiseGraphics(const FRAMEBUFFER_INFORMATION& info, void* ktstat);
void StartupGraphics();

CHAIKRNL_FUNC void* CreateStdioWindow(size_t width, size_t height, size_t xpos = 0, size_t ypos = 0);
void SetWindowPostion(void* wnd, size_t xpos, size_t ypos);
void* CreateWindow(size_t width, size_t height);
void* CreateWindow(void* wnd);
void CopyWindow(void* wnd);
void SetBackgroundColour(COLORREF col);


#endif