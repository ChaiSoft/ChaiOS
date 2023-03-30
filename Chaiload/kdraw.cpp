#include "kdraw.h"
#include "kterm.h"
#include "asciifont.h"

static void* framebuffer = nullptr;
static size_t framebuffer_sz = 0;
static size_t pixelsPerLine = 0;
static uint32_t redm, greenm, bluem, resvm;
static size_t h_res, v_res;
static size_t bpp;

static int LINES = 20;

void set_scrolllines(int lines)
{
	LINES = lines;
}

static void* basic_memcpy(void* dst, const void* src, size_t length)
{
	for (size_t n = 0; n < length; ++n)
		((char*)dst)[n] = ((char*)src)[n];
	return dst;
}
static memcpy_proc memcpy = &basic_memcpy;

void set_memcpy(memcpy_proc memcp)
{
	memcpy = memcp;
}

static int_fast8_t high_set_bit(size_t sz)
{
	int_fast8_t count = -1;
	while (sz)
	{
		sz >>= 1;
		count += 1;
	}
	return count;
}

static int_fast8_t low_set_bit(size_t sz)
{
	int_fast8_t count = -1;
	while (sz)
	{
		count += 1;
		if ((sz & 1) != 0)
			break;
		sz >>= 1;
	}
	return count;
}

void InitialiseGraphics(const FRAMEBUFFER_INFORMATION& info)
{
	framebuffer = info.phyaddr;
	framebuffer_sz = info.size;
	pixelsPerLine = info.pixelsPerLine;
	redm = info.redmask; greenm = info.greenmask; bluem = info.bluemask; resvm = info.resvmask;
	size_t mergemasks = redm | greenm | bluem | resvm;
	h_res = info.Horizontal; v_res = info.Vertical;
	bpp = high_set_bit(mergemasks) + 1;
}

static size_t xpos = 0;
static size_t ypos = 0;

struct kterm_status {
	size_t* x;
	size_t* y;
};

static kterm_status stat = { &xpos, &ypos };

void populate_kterm_info(void*& inf)
{
	inf = &stat;
}

static COLORREF foreground = RGB(255, 187, 0);
static COLORREF background = RGB(0, 0, 0xAA);

void putpixel(size_t x, size_t y, COLORREF col)
{
	uint32_t* pixelloc = raw_offset<uint32_t*>(framebuffer, (pixelsPerLine*y + x)*(bpp/8));
	size_t pixel = ((RED(col) << low_set_bit(redm))&redm) | ((GREEN(col) << low_set_bit(greenm))&greenm) | ((BLUE(col) << low_set_bit(bluem))&bluem) | (*pixelloc & resvm);
	*pixelloc = pixel;
}


void gputs_k(const char16_t* str)
{
	while (*str)
	{
		if (*str > 0xFF)
		{
			//No unicode support at the moment
		}
		else if (*str == '\n')
		{
			++ypos;
			xpos = 0;
		}
		else if (*str == '\r')
		{

		}
		else if (*str == '\b')
		{
			if (xpos > 0)
				--xpos;
		}
		else
		{
			//Draw the character
			const bx_fontcharbitmap_t& entry = bx_vgafont[*str];
			for (size_t y = 0; y < 16; ++y)
			{
				for (size_t x = 0; x < 8; ++x)
				{
					if (entry.data[y] & (1 << x))
					{
						putpixel(x + xpos * 9, y + ypos * 16, foreground);
					}
					else
					{
						putpixel(x + xpos * 9, y + ypos * 16, background);
					}
				}
				putpixel(8 + xpos * 9, y + ypos * 16, background);
			}
			++xpos;
			if (xpos > h_res / 9)
			{
				xpos = 0;
				++ypos;
			}
		}
		if (ypos >= v_res / 16 - 1)
		{
			if (LINES > 0)
			{
				void* dest = raw_offset<void*>(framebuffer, (16 * (ypos - LINES) * pixelsPerLine)*(bpp / 8));
				void* src = raw_offset<void*>(framebuffer, (16 * (ypos - (LINES - 1)) * pixelsPerLine)*(bpp / 8));
				memcpy(dest, src, (((LINES - 1) * 16)*pixelsPerLine)*(bpp / 8));
			}
			--ypos;
			//memcpy(framebuffer, raw_offset<void*>(framebuffer, (16*pixelsPerLine)*(bpp / 8)), ((v_res - 16)*pixelsPerLine)*(bpp / 8));
			for (size_t n = 0; n < 16; ++n)
			{
				for (size_t x = 0; x < h_res; ++x)
				{
					putpixel(x, n + ypos * 16, background);
				}
			}
			//ypos = 0;
		}
		++str;
	}
}