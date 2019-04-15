#ifndef CHAIOS_VA_ARGS_H
#define CHAIOS_VA_ARGS_H

#include <stdint.h>

//typedef unsigned char* va_list;

#define	VA_SIZE(TYPE) ((sizeof(TYPE) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))

#define	va_start(AP, LASTARG) (AP=((va_list)&(LASTARG) + VA_SIZE(LASTARG)))
#define va_end(AP)
#define va_arg(AP, TYPE) (AP += VA_SIZE(TYPE), *((TYPE *)(AP - VA_SIZE(TYPE))))


#endif
