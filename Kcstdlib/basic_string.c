#include <string.h>

EXTERN KCSTDLIB_FUNC int toupper(int c)
{
	if (c >= 'a' && c <= 'z')
		c = c - 'a' + 'A';
	return c;
}

EXTERN KCSTDLIB_FUNC int tolower(int c)
{
	if (c >= 'A' && c <= 'Z')
		c = c - 'A' + 'a';
	return c;
}

EXTERN KCSTDLIB_FUNC int isprint(int c)
{
	return (c > 0x1f) && (c != 0x7f);
}

EXTERN KCSTDLIB_FUNC int isdigit(int c)
{
	return (c >= '0') && (c <= '9');
}
EXTERN KCSTDLIB_FUNC int isxdigit(int c)
{
	return ((c >= 'A') && (c <= 'F')) || isdigit(c);
}
EXTERN KCSTDLIB_FUNC int isspace(int c)
{
	return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\v') || (c == '\f') || (c == '\r');
}

EXTERN KCSTDLIB_FUNC int strncmp(const char* s1, const char* s2, size_t length)
{
	for (; *s1 && *s1 == *s2 && length > 0; --length, ++s1, ++s2);
	return (*s1 - *s2);
}

EXTERN KCSTDLIB_FUNC char* strncpy(char* s1, const char* s2, size_t length)
{
	for (size_t n = 0; s2[n] && n < length; ++n)
		s1[n] = s2[n];
	return s1;
}

EXTERN KCSTDLIB_FUNC char* strncat(char* destination, const char* source, size_t num)
{
	char* tmp = destination;
	for (; *tmp; ++tmp);
	strncpy(tmp, source, num);
	return tmp;
}