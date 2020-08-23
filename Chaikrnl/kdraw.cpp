#include "kdraw.h"

#include "asciifont.h"
#include <arch/paging.h>
#include <string.h>

static size_t framebuffer_sz = 0;
static uint32_t redm, greenm, bluem, resvm;
static size_t bpp;

typedef struct {
	void* framebuffer;
	size_t pixelsPerLine;
	size_t h_res;
	size_t v_res;
	size_t text_xpos;
	size_t text_ypos;
	size_t xpos;
	size_t ypos;

	size_t x_min_invd;
	size_t x_max_invd;
	size_t y_min_invd;
	size_t y_max_invd;
} gbuf_desc;

static gbuf_desc screen_desc;

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

struct kterm_status {
	size_t* x;
	size_t* y;
};

static COLORREF foreground = RGB(255, 187, 0);
static COLORREF background = RGB(0, 0, 0xAA);

void SetBackgroundColour(COLORREF col)
{
	background = col;
}

static void(*putpixel)(gbuf_desc* buf, size_t x, size_t y, COLORREF col);

static void putpixel_slow(gbuf_desc* buf, size_t x, size_t y, COLORREF col)
{
	uint32_t* pixelloc = raw_offset<uint32_t*>(buf->framebuffer, (buf->pixelsPerLine*y + x)*(bpp / 8));
	size_t pixel = ((RED(col) << low_set_bit(redm))&redm) | ((GREEN(col) << low_set_bit(greenm))&greenm) | ((BLUE(col) << low_set_bit(bluem))&bluem) | (*pixelloc & resvm);
	*pixelloc = pixel;
}

static void putpixel_rrRRGGBB(gbuf_desc* buf, size_t x, size_t y, COLORREF col)
{
	uint32_t* pixelloc = raw_offset<uint32_t*>(buf->framebuffer, (buf->pixelsPerLine*y + x)*(bpp / 8));
	size_t pixel = (RED(col) << 16) | (GREEN(col) << 8) | (BLUE(col)) | (*pixelloc & resvm);
	*pixelloc = pixel;
}

static void putpixel_BBGGRRrr(gbuf_desc* buf, size_t x, size_t y, COLORREF col)
{
	uint32_t* pixelloc = raw_offset<uint32_t*>(buf->framebuffer, (buf->pixelsPerLine*y + x)*(bpp / 8));
	size_t pixel = (RED(col) << 16) | (GREEN(col) << 8) | (BLUE(col)) | (*pixelloc & resvm);
	*pixelloc = pixel;
}
#include <kstdio.h>

static void BitBlt(gbuf_desc* dst_buf, size_t x_dst, size_t y_dst, size_t width, size_t height, gbuf_desc* src_buf, size_t x_src, size_t y_src)
{
	for (int i = 0; i < height; ++i)
	{
		size_t dstoff = ((y_dst+i)*dst_buf->pixelsPerLine + x_dst)*(bpp / 8);
		size_t srcoff = ((y_src + i)*src_buf->pixelsPerLine + x_src)*(bpp / 8);
		size_t linecount = width * (bpp / 8);
		//kprintf(u"Memcpy: %x[%d] -> %x[%d], len %d\n", src_buf->framebuffer, srcoff, dst_buf->framebuffer, dstoff, linecount);
		void* dstline = raw_offset<void*>(dst_buf->framebuffer, dstoff);
		void* srcline = raw_offset<void*>(src_buf->framebuffer, srcoff);
		memcpy(dstline, srcline, linecount);
	}
}

static void DrawRect(gbuf_desc* gbuf, size_t x_dst, size_t y_dst, size_t width, size_t height, COLORREF col)
{
	for (size_t y = 0; y < height; ++y)
	{
		for (size_t x = 0; x < width; ++x)
		{
			putpixel(gbuf, x_dst + x, y_dst + y, col);
		}
	}
}

void InitialiseGraphics(const FRAMEBUFFER_INFORMATION& info, void* kterm_st)
{
	screen_desc.framebuffer = find_free_paging(info.size);
	paging_map(screen_desc.framebuffer, (paddr_t)info.phyaddr, info.size, PAGE_ATTRIBUTE_WRITABLE);
	framebuffer_sz = info.size;
	screen_desc.pixelsPerLine = info.pixelsPerLine;
	redm = info.redmask; greenm = info.greenmask; bluem = info.bluemask; resvm = info.resvmask;
	size_t mergemasks = redm | greenm | bluem | resvm;
	bpp = high_set_bit(mergemasks) + 1;
	kterm_status* stat = (kterm_status*)kterm_st;
	screen_desc.text_xpos = *stat->x;
	screen_desc.text_ypos = *stat->y;
	if ((info.redmask == 0xFF0000) && (info.greenmask == 0x00FF00) && (info.bluemask == 0x0000FF) && (info.resvmask == 0xFF000000))
	{
		putpixel = &putpixel_rrRRGGBB;
	}
	else if ((info.redmask == 0xFF00) && (info.greenmask == 0x00FF0000) && (info.bluemask == 0xFF000000) && (info.resvmask == 0x0000FF))
	{
		putpixel = &putpixel_BBGGRRrr;
	}
	else
	{
		putpixel = &putpixel_slow;
	}
	screen_desc.h_res = info.Horizontal;
	screen_desc.v_res = info.Vertical;
	screen_desc.xpos = screen_desc.ypos = 0;
}

void* CreateWindow(size_t width, size_t height)
{
	gbuf_desc* wnd = new gbuf_desc;
	wnd->pixelsPerLine = width;
	size_t bufsz = height * width * (bpp / 8);
	wnd->framebuffer = new uint8_t[bufsz];
	wnd->h_res = width;
	wnd->v_res = height;
	wnd->text_xpos = 0;
	wnd->text_ypos = 0;
	wnd->xpos = 0;
	wnd->ypos = 0;
	wnd->x_min_invd = wnd->y_min_invd = 0;
	wnd->x_max_invd = width;
	wnd->y_max_invd = height;

	DrawRect(wnd, 0, 0, width, height, background);
	return wnd;
}

void CopyWindow(void* wnd)
{
	gbuf_desc* dsc = (gbuf_desc*)wnd;
	if(dsc->x_min_invd != SIZE_MAX && dsc->y_min_invd != SIZE_MAX)		//No change
		BitBlt(&screen_desc, dsc->xpos + dsc->x_min_invd, dsc->ypos + dsc->y_min_invd, dsc->x_max_invd - dsc->x_min_invd, dsc->y_max_invd - dsc->y_min_invd, dsc, dsc->x_min_invd, dsc->y_min_invd);
	dsc->x_min_invd = dsc->y_min_invd = SIZE_MAX;
	dsc->x_max_invd = dsc->y_max_invd = 0;
}

static void* koutwnd = nullptr;

void SetKoutWindow(void* wnd)
{
	koutwnd = wnd;
}

void gputs_k(const char16_t* str, void* wnd)
{
	gbuf_desc* buf = (gbuf_desc*)wnd;
	if (!wnd)
		buf = &screen_desc;
	while (*str)
	{
		if (*str > 0xFF)
		{
			//No unicode support at the moment
		}
		else if (*str == '\n')
		{
			++buf->text_ypos;
			buf->text_xpos = 0;
		}
		else if (*str == '\r')
		{

		}
		else if (*str == '\b')
		{
			if (buf->text_xpos > 0)
				--buf->text_xpos;
		}
		else
		{
			//Draw the character
			const bx_fontcharbitmap_t& entry = bx_vgafont[*str];
			if (buf->x_min_invd > buf->text_xpos * 9)
				buf->x_min_invd = buf->text_xpos * 9;
			if (buf->y_min_invd > buf->text_ypos * 16)
				buf->y_min_invd = buf->text_ypos * 16;
			if (buf->x_max_invd < buf->text_xpos * 9 + 8)
				buf->x_max_invd = buf->text_xpos * 9 + 8;
			if (buf->y_max_invd < buf->text_ypos * 16 + 16)
				buf->y_max_invd = buf->text_ypos * 16 + 16;
			for (size_t y = 0; y < 16; ++y)
			{
				for (size_t x = 0; x < 8; ++x)
				{
					if (entry.data[y] & (1 << x))
					{
						putpixel(buf, x + buf->text_xpos * 9, y + buf->text_ypos * 16, foreground);
					}
					else
					{
						putpixel(buf, x + buf->text_xpos * 9, y + buf->text_ypos * 16, background);
					}
				}
				putpixel(buf, 8 + buf->text_xpos * 9, y + buf->text_ypos * 16, background);
			}
			++buf->text_xpos;
			if (buf->text_xpos + 1 > buf->h_res / 9)
			{
				buf->text_xpos = 0;
				++buf->text_ypos;
			}
		}
		if (buf->text_ypos + 1 > buf->v_res / 16)
		{
			if (wnd)
			{
				size_t lastline = (buf->v_res / 16 - 1) * 16;
				BitBlt(buf, 0, 0, buf->h_res, lastline, buf, 0, 16);
				DrawRect(buf, 0, lastline, buf->h_res, buf->v_res - lastline, background);
				buf->x_min_invd = buf->y_min_invd = 0;
				buf->x_max_invd = buf->h_res;
				buf->y_max_invd = buf->v_res;
				--buf->text_ypos;
			}
			else
				buf->text_ypos = 0;

		}
		++str;
	}
}

void gputs_k(const char16_t* str)
{
	gputs_k(str, koutwnd);
	if (koutwnd)
		CopyWindow(koutwnd);
}