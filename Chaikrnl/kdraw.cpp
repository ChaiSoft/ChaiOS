#include "kdraw.h"

#include "asciifont.h"
#include <arch/paging.h>

static void* framebuffer = nullptr;
static size_t framebuffer_sz = 0;
static size_t pixelsPerLine = 0;
static uint32_t redm, greenm, bluem, resvm;
static size_t h_res, v_res;
static size_t bpp;

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

static size_t xpos = 0;
static size_t ypos = 0;

struct kterm_status {
	size_t* x;
	size_t* y;
};

void InitialiseGraphics(const FRAMEBUFFER_INFORMATION& info, void* kterm_st)
{
	framebuffer = find_free_paging(info.size);
	paging_map(framebuffer, (paddr_t)info.phyaddr, info.size, PAGE_ATTRIBUTE_WRITABLE);
	framebuffer_sz = info.size;
	pixelsPerLine = info.pixelsPerLine;
	redm = info.redmask; greenm = info.greenmask; bluem = info.bluemask; resvm = info.resvmask;
	size_t mergemasks = redm | greenm | bluem | resvm;
	h_res = info.Horizontal; v_res = info.Vertical;
	bpp = high_set_bit(mergemasks) + 1;
	kterm_status* stat = (kterm_status*)kterm_st;
	xpos = *stat->x;
	ypos = *stat->y;
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
		if (ypos+1 > v_res / 16)
			ypos = 0;
		++str;
	}
}