#include <string.h>


EXTERN size_t strlen(const char* str)
{
	size_t result = 0;
	while (*str++)
		++result;
	return result;
}
