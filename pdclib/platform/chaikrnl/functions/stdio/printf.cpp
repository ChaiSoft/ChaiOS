#include <kstdio.h>
#include <vaargs.h>

static chaios_stdio_puts_proc puts_s;
static chaios_read_stdio_handle puts_handle;
void kputs(const char* s);

#define EXTERN extern "C"

EXTERN KCSTDLIB_FUNC void set_stdio_puts(chaios_stdio_puts_proc putsp, chaios_read_stdio_handle stdhandle)
{
	puts_s = putsp;
	puts_handle = stdhandle;
}

EXTERN KCSTDLIB_FUNC void kputs(const char16_t* str)
{
	void* hnd = NULL;
	if(puts_handle)
		hnd = puts_handle();
	puts_s(str, hnd);
}

EXTERN KCSTDLIB_FUNC void kputsWnd(const char16_t* str, void* wnd)
{
	puts_s(str, wnd);
}

static int ltolower(int c)
{
	if (c >= 'A' && c <= 'Z')
	{
		c = (c - 'A') + 'a';
	}
	return c;
}
static int lisdigit(int c)
{
	if (c >= '0' && c <= '9')
		return 1;
	else
		return 0;
}

/*---------------------------------------------------*/
/* Public Domain version of printf                   */
/*                                                   */
/* Rud Merriam, Compsult, Inc. Houston, Tx.          */
/*                                                   */
/* For Embedded Systems Programming, 1991            */
/*                                                   */
/*---------------------------------------------------*/

#include <ctype.h>
#include <string.h>
#include <stdarg.h>

/*---------------------------------------------------*/
/* The purpose of this routine is to output data the */
/* same as the standard printf function without the  */
/* overhead most run-time libraries involve. Usually */
/* the printf brings in many kiolbytes of code and   */
/* that is unacceptable in most embedded systems.    */
/*---------------------------------------------------*/

typedef const char* charptr;
typedef int(*func_ptr)(int c);

static func_ptr out_char;
static int do_padding;
static int left_flag;
static int len;
static int num1;
static int num2;
static char pad_character;

/*---------------------------------------------------*/
/*                                                   */
/* This routine puts pad characters into the output  */
/* buffer.                                           */
/*                                                   */
static void padding(const int l_flag)
{
	int i;

	if (do_padding && l_flag && (len < num1))
		for (i = len; i < num1; i++)
			out_char(pad_character);
}

/*---------------------------------------------------*/
/*                                                   */
/* This routine moves a string to the output buffer  */
/* as directed by the padding and positioning flags. */
/*                                                   */
static void outs(charptr lp)
{
	/* pad on left if needed                          */
	len = strlen(lp);
	padding(!left_flag);

	/* Move string to the buffer                      */
	while (*lp && num2--)
		out_char(*lp++);

	/* Pad on right if needed                         */
	len = strlen(lp);
	padding(left_flag);
}

/*---------------------------------------------------*/
/*                                                   */
/* This routine moves a number to the output buffer  */
/* as directed by the padding and positioning flags. */
/*                                                   */
static void outnum(int64_t num, const long base)
{
	char* cp;
	int negative;
	char outbuf[64];
	const char digits[] = "0123456789ABCDEF";

	uint64_t unsignedValue = (uint64_t)num;
	/* Check if number is negative                    */
	/* NAK 2009-07-29 Negate the number only if it is not a hex value. */
	if ((int64_t)num < 0L && base != 16L) {
		negative = 1;
		num = -num;
		unsignedValue = num;
	}
	else
		negative = 0;

	/* Build number (backwards) in outbuf             */
	cp = outbuf;
	do {
		*cp++ = digits[(uint64_t)(unsignedValue % base)];
	} while ((unsignedValue /= base) > 0);
	if (negative)
		*cp++ = '-';
	*cp-- = 0;

	/* Move the converted number to the buffer and    */
	/* add in the padding where needed.               */
	len = strlen(outbuf);
	padding(!left_flag);
	while (cp >= outbuf)
		out_char(*cp--);
	padding(left_flag);
}

/*---------------------------------------------------*/
/*                                                   */
/* This routine gets a number from the format        */
/* string.                                           */
/*                                                   */
static int getnum(charptr* linep)
{
	int n;
	charptr cp;

	n = 0;
	cp = *linep;
	while (lisdigit(*cp))
		n = n * 10 + ((*cp++) - '0');
	*linep = cp;
	return(n);
}

/*---------------------------------------------------*/
/*                                                   */
/* This routine operates just like a printf/sprintf  */
/* routine. It outputs a set of data under the       */
/* control of a formatting string. Not all of the    */
/* standard C format control are supported. The ones */
/* provided are primarily those needed for embedded  */
/* systems work. Primarily the floaing point         */
/* routines are omitted. Other formats could be      */
/* added easily by following the examples shown for  */
/* the supported formats.                            */
/*                                                   */

void esp_vprintf(const func_ptr f_ptr,
	charptr ctrl, va_list argp)
{

	int long_flag;
	int dot_flag;
	char ch;


	out_char = f_ptr;

	for (; *ctrl; ctrl++) {

		/* move format string chars to buffer until a  */
		/* format control is found.                    */
		if (*ctrl != '%') {
			out_char(*ctrl);
			continue;
		}

		/* initialize all the flags for this format.   */
		dot_flag =
			long_flag =
			left_flag =
			do_padding = 0;
		pad_character = ' ';
		num2 = 32767;

	try_next:
		ch = *(++ctrl);

		if (lisdigit(ch)) {
			if (dot_flag)
				num2 = getnum(&ctrl);
			else {
				if (ch == '0')
					pad_character = '0';

				num1 = getnum(&ctrl);
				do_padding = 1;
			}
			ctrl--;
			goto try_next;
		}

		switch (ltolower(ch)) {
		case '%':
			out_char('%');
			continue;

		case '-':
			left_flag = 1;
			break;

		case '.':
			dot_flag = 1;
			break;

		case 'l':
			long_flag = 1;
			break;

		case 'i':
		case 'd':
			if (long_flag || ch == 'D') {
				outnum(va_arg(argp, int64_t), 10L);
				continue;
			}
			else {
				outnum(va_arg(argp, int), 10L);
				continue;
			}
		case 'x':
			if (long_flag) {
				outnum(va_arg(argp, uint64_t), 16L);
				continue;
			}
			else {
				outnum(va_arg(argp, unsigned int), 16L);
				continue;
			}
			continue;

		case 's':
			outs(va_arg(argp, charptr));
			continue;

		case 'c':
			out_char(va_arg(argp, int));
			continue;

		case '\\':
			switch (*ctrl) {
			case 'a':
				out_char(0x07);
				break;
			case 'h':
				out_char(0x08);
				break;
			case 'r':
				out_char(0x0D);
				break;
			case 'n':
				out_char(0x0D);
				out_char(0x0A);
				break;
			default:
				out_char(*ctrl);
				break;
			}
			ctrl++;
			break;

		default:
			continue;
		}
		goto try_next;
	}
	va_end(argp);
}

/*---------------------------------------------------*/


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

static const char* chars = "0123456789ABCDEF";
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

#include <spinlock.h>
spinlock_t screenlock = nullptr;
bool need_lock = false;

EXTERN KCSTDLIB_FUNC void enable_screen_locking()
{
	need_lock = true;
}

EXTERN KCSTDLIB_FUNC void kprintf(const char16_t* format, ...)
{
	cpu_status_t st;
	if (need_lock)
	{
		if (!screenlock)
			screenlock = create_spinlock();
		st = acquire_spinlock(screenlock);
	}
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
				size_t length = strlen_simple(buffer);
				while (length++ < width)
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
	if(need_lock)
		release_spinlock(screenlock, st);
}

EXTERN KCSTDLIB_FUNC void kvprintf(const char* format, va_list args)
{
	bool lastwasformat = false;
	while (*format != '\0') {
		if (lastwasformat)
		{
			while (*format == '.' || (*format >= '0' && *format <= '9') || *format == '-')
				++format;
		}
		if (*format == '%') {
			lastwasformat = true;
			++format;
			while (*format == '.' || (*format >= '0' && *format <= '9') || *format == '-')
				++format;
			if (*format == 'd' || *format == 'u') {
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
				size_t length = strlen_simple(buffer);
				while (length++ < width)
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
			else if (*format == 'x' || *format == 'p' || *format == 'X') {
				size_t x = va_arg(args, size_t);
				char16_t buffer[sizeof(size_t) * 8 + 1];
				sztoa(x, buffer, 16);
				kputs(buffer);
			}
			else if (*format == 's') {
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
			lastwasformat = false;
		}
		++format;
	}
}

void atow(char16_t* buf, const char* source)
{
	while (*source != 0)*buf++ = *source++;
	*buf = u'\0';
}


void kputs(const char* s)
{
	for (;*s;++s)
	{
		char16_t vals[2];
		vals[0] = *s;
		vals[1] = 0;
		kputs(vals);
	}
}

static int putc_ascii(int c)
{
	char16_t outp[2];
	outp[0] = c;
	outp[1] = 0;
	kputs(outp);
	return 0;
}

EXTERN KCSTDLIB_FUNC void kvprintf_a(const char* format, va_list args)
{
	cpu_status_t st;
	if(need_lock)
		st = acquire_spinlock(screenlock);
	//kvprintf(format, args);
	esp_vprintf(&putc_ascii, format, args);
	if(need_lock)
		release_spinlock(screenlock, st);
}

EXTERN KCSTDLIB_FUNC void kprintf_a(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	kvprintf_a(format, args);
	va_end(args);
}