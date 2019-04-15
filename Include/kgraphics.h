#ifndef CHAIOS_KGRAPHICS_H
#define CHAIOS_KGRAPHICS_H

#include <kernelinfo.h>

typedef struct _POINT {
	uint32_t x;
	uint32_t y;
}POINT, *PPOINT;

void graphics_initialize(PFRAMEBUFFER_INFORMATION fbinfo);

#endif
