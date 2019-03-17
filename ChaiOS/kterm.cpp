#include "kterm.h"
#include <acpi.h>

static void(*puts_s)(const char16_t*) = nullptr;
void setKtermOutputProc(void(*putsp)(const char16_t*))
{
	puts_s = putsp;
}

void puts(const char16_t* s)
{
	puts_s(s);
}

static char* chars = "0123456789ABCDEF";
char*  sztoa(size_t value, char16_t * str, int base)
{
	if (base < 2 || base > 16)
		return nullptr;
	unsigned int i = 0;
	do {
		str[++i] = chars[value%base];
		value /= base;
	} while (value != 0);
	str[0] = u'\0';
	for (unsigned int z = 0; z < i; ++z, --i)
	{
		char16_t tmp = str[z];
		str[z] = str[i];
		str[i] = tmp;
	}
}

void printf(const char16_t* format, ...)
{
	va_list args;
	va_start(args, format);
	while (*format != '\0') {
		if (*format == '%') {
			++format;
			if (*format == 'd') {
				int i = va_arg(args, int);
				char16_t buffer[sizeof(size_t) * 8 + 1];
				sztoa(i, buffer, 10);
				puts(buffer);
			}
			else if (*format == 'c') {
				// A 'char' variable will be promoted to 'int'
				// A character literal in C is already 'int' by itself
				int c = va_arg(args, int);
				char16_t buffer[sizeof(size_t) * 8 + 1];
				sztoa(c, buffer, 10);
				puts(buffer);
			}
			else if (*format == 'x') {
				size_t x = va_arg(args, size_t);
				char16_t buffer[sizeof(size_t) * 8 + 1];
				sztoa(x, buffer, 16);
				puts(u"0x");
				puts(buffer);
			}
			else if (*format == '%') {
				puts(u"%");
			}
			else
			{
				char16_t buf[3];
				buf[0] = u'%'; buf[1] = *format; buf[2] = u'\0';
				puts(buf);
			}
		}
		else
		{
			char16_t buf[2];
			buf[0] = *format; buf[1] = u'\0';
			puts(buf);
		}
		++format;
	}

	va_end(args);
}