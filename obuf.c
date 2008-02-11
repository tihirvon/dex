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
