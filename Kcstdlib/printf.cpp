#include <kstdio.h>
#include <vaargs.h>

static chaios_stdio_puts_proc puts_s;
void kputs(const char* s);

EXTERN KCSTDLIB_FUNC void set_stdio_puts(chaios_stdio_puts_proc putsp)
{
	puts_s = putsp;
}

EXTERN KCSTDLIB_FUNC void kputs(const char16_t* str)
{
	puts_s(str);
}

static size_t strlen_simple(const char* s)
{
	size_t l = 0;
	while (*s++)++l;
	return l;
}

static size_t strlen_simple(const char16_t* s)
{
	size_t l = 0;
	while (*s++)++l;
	return l;
}

static char* chars = "0123456789ABCDEF";
char16_t*  sztoa(size_t value, char16_t * str, int base)
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
	return str;
}

EXTERN KCSTDLIB_FUNC void kprintf(const char16_t* format, ...)
{
	va_list args;
	va_start(args, format);
	while (*format != '\0') {
		if (*format == '%') {
			++format;
			if (*format == 'd') {
				size_t width = 0;
				if (format[1] == '.')
				{
					for (size_t i = 2; format[i] >= '0' && format[i] <= '9'; ++i)
					{
						width *= 10;
						width += format[i] - '0';
					}
				}
				size_t i = va_arg(args, size_t);
				char16_t buffer[sizeof(size_t) * 8 + 1];
				sztoa(i, buffer, 10);
				size_t len = strlen_simple(buffer);
				while (len++ < width)
					kputs(u"0");
				kputs(buffer);
			}
			else if (*format == 'c') {
				// A 'char' variable will be promoted to 'int'
				// A character literal in C is already 'int' by itself
				int c = va_arg(args, int);
				char16_t buffer[sizeof(size_t) * 8 + 1];
				sztoa(c, buffer, 10);
				kputs(buffer);
			}
			else if (*format == 'x') {
				size_t x = va_arg(args, size_t);
				char16_t buffer[sizeof(size_t) * 8 + 1];
				sztoa(x, buffer, 16);
				kputs(u"0x");
				kputs(buffer);
			}
			else if (*format == 's') {
				char16_t* x = va_arg(args, char16_t*);
				kputs(x);
			}
			else if (*format == 'S') {
				char* x = va_arg(args, char*);
				kputs(x);
			}
			else if (*format == '%') {
				kputs(u"%");
			}
			else if (*format == '.') {
				kputs(u".");
			}
			else
			{
				char16_t buf[3];
				buf[0] = u'%'; buf[1] = *format; buf[2] = u'\0';
				kputs(buf);
			}
		}
		else
		{
			char16_t buf[2];
			buf[0] = *format; buf[1] = u'\0';
			kputs(buf);
		}
		++format;
	}

	va_end(args);
}

void atow(char16_t* buf, const char* source)
{
	while (*source != 0)*buf++ = *source++;
	*buf = u'\0';
}


void kputs(const char* s)
{
	char16_t* buffer = new char16_t[strlen_simple(s) + 1];
	atow(buffer, s);
	kputs(buffer);
}