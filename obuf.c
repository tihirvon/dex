#include "obuf.h"
#include "term.h"
#include "buffer.h"

struct output_buffer obuf;

// does not update obuf.x
void buf_add_bytes(const char *str, int count)
{
	while (count) {
		unsigned int avail = obuf.alloc - obuf.count;
		if (count <= avail) {
			memcpy(obuf.buf + obuf.count, str, count);
			obuf.count += count;
			break;
		} else {
			buf_flush();
			if (count >= obuf.alloc) {
				xwrite(1, str, count);
				break;
			}
		}
	}
}

// does not update obuf.x
void buf_set_bytes(char ch, int count)
{
	while (count) {
		unsigned int avail = obuf.alloc - obuf.count;
		if (count <= avail) {
			memset(obuf.buf + obuf.count, ch, count);
			obuf.count += count;
			break;
		} else {
			if (avail) {
				memset(obuf.buf + obuf.count, ch, avail);
				obuf.count += avail;
				count -= avail;
			}
			buf_flush();
		}
	}
}

void buf_escape(const char *str)
{
	buf_add_bytes(str, strlen(str));
}

// width of ch must be 1
void buf_ch(char ch)
{
	if (obuf.x >= obuf.scroll_x && obuf.x < obuf.width + obuf.scroll_x) {
		if (obuf.count == obuf.alloc)
			buf_flush();
		obuf.buf[obuf.count++] = ch;
	}
	obuf.x++;
}

void buf_hide_cursor(void)
{
	if (term_cap.vi)
		buf_escape(term_cap.vi);
}

void buf_show_cursor(void)
{
	if (term_cap.ve)
		buf_escape(term_cap.ve);
}

void buf_move_cursor(int x, int y)
{
	buf_escape(term_move_cursor(x, y));
	obuf.x = x;
}

void buf_set_colors(int fg, int bg)
{
	buf_escape(term_set_colors(fg, bg));
	obuf.bg = bg;
}

void buf_clear_eol(void)
{
	if (obuf.x < obuf.scroll_x + obuf.width) {
		if (term_cap.ce && (obuf.bg == -1 || term_cap.ut)) {
			buf_escape(term_cap.ce);
		} else {
			buf_set_bytes(' ', obuf.scroll_x + obuf.width - obuf.x);
		}
		obuf.x = obuf.scroll_x + obuf.width;
	}
}

void buf_flush(void)
{
	if (obuf.count) {
		xwrite(1, obuf.buf, obuf.count);
		obuf.count = 0;
	}
}

static void skipped_too_much(uchar u)
{
	int n = obuf.x - obuf.scroll_x;

	if (obuf.alloc - obuf.count < 8)
		buf_flush();
	if (u == '\t') {
		memset(obuf.buf + obuf.count, ' ', n);
		obuf.count += n;
	} else if (u < 0x20) {
		obuf.buf[obuf.count++] = u | 0x40;
	} else if (u & U_INVALID_MASK) {
		if (n > 2)
			obuf.buf[obuf.count++] = hex_tab[(u >> 4) & 0x0f];
		if (n > 1)
			obuf.buf[obuf.count++] = hex_tab[u & 0x0f];
		obuf.buf[obuf.count++] = '>';
	} else {
		obuf.buf[obuf.count++] = '>';
	}
}

void buf_skip(uchar u, int utf8)
{
	if (u < 0x80 || !utf8) {
		if (u >= 0x20) {
			obuf.x++;
		} else if (u == '\t') {
			obuf.x += (obuf.x + obuf.tab_width) / obuf.tab_width * obuf.tab_width - obuf.x;
		} else {
			// control
			obuf.x += 2;
		}
	} else {
		obuf.x += u_char_width(u);
	}

	if (obuf.x > obuf.scroll_x)
		skipped_too_much(u);
}

int buf_put_char(uchar u, int utf8)
{
	unsigned int space = obuf.scroll_x + obuf.width - obuf.x;
	unsigned int width;

	if (!space)
		return 0;
	if (obuf.alloc - obuf.count < 8)
		buf_flush();

	if (u < 0x80 || !utf8) {
		if (u >= 0x20) {
			obuf.buf[obuf.count++] = u;
			obuf.x++;
		} else if (u == '\t') {
			width = (obuf.x + obuf.tab_width) / obuf.tab_width * obuf.tab_width - obuf.x;
			if (width > space)
				width = space;
			memset(obuf.buf + obuf.count, ' ', width);
			obuf.count += width;
			obuf.x += width;
		} else {
			obuf.buf[obuf.count++] = '^';
			obuf.x++;
			if (space > 1) {
				obuf.buf[obuf.count++] = u | 0x40;
				obuf.x++;
			}
		}
	} else {
		width = u_char_width(u);
		if (width <= space) {
			u_set_char(obuf.buf, &obuf.count, u);
			obuf.x += width;
		} else {
			obuf.buf[obuf.count++] = '>';
			obuf.x++;
		}
	}
	return 1;
}
