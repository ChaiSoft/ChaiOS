#ifndef CHAIOS_KCSTDLIB_STRING_H
#define CHAIOS_KCSTDLIB_STRING_H

#include "kcstdlib.h"
#include <stdint.h>

EXTERN KCSTDLIB_FUNC void* memcpy(void* destination, const void* source, size_t num);
EXTERN KCSTDLIB_FUNC void* memset(void* destination, int val, size_t num);
EXTERN KCSTDLIB_FUNC int memcmp(const void* destination, const void* source, size_t num);
EXTERN KCSTDLIB_FUNC int strncmp(const char* s1, const char* s2, size_t length);
EXTERN KCSTDLIB_FUNC char* strncpy(char* dest, const char* src, size_t length);
EXTERN KCSTDLIB_FUNC char* strncat(char* destination, const char* source, size_t num);
EXTERN KCSTDLIB_FUNC int toupper(int c);
EXTERN KCSTDLIB_FUNC int isprint(int c);
EXTERN KCSTDLIB_FUNC int isdigit(int c);
EXTERN KCSTDLIB_FUNC int isxdigit(int c);
EXTERN KCSTDLIB_FUNC int isspace(int c);

#endif
