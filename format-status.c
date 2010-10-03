#include "format-status.h"
#include "term.h"
#include "window.h"

static int separator;

static void add_status_str(char *buf, int size, int *posp, const char *str)
{
	unsigned int pos = *posp;
	unsigned int idx = 0;

	if (!*str)
		return;

	if (separator) {
		if (pos + 2 < size)
			buf[pos++] = ' ';
		separator = 0;
	}
	if (term_flags & TERM_UTF8) {
		while (pos < size && str[idx]) {
			uchar u = u_buf_get_char(str, idx + 4, &idx);
			u_set_char(buf, &pos, u);
		}
	} else {
		while (pos < size && str[idx]) {
			unsigned char ch = str[idx++];
			if (ch < 0x20) {
				buf[pos++] = '^';
				buf[pos++] = ch | 0x40;
			} else if (ch == 0x7f) {
				buf[pos++] = '^';
				buf[pos++] = '?';
			} else {
				buf[pos++] = ch;
			}
		}
	}
	*posp = pos;
}

__FORMAT(1, 2)
static const char *ssprintf(const char *format, ...)
{
	static char buf[256];
	va_list ap;

	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	return buf;
}

static void add_status_pos(char *buf, int size, int *posp)
{
	int h = window->h;
	int pos = view->vy;

	if (buffer->nl <= h) {
		if (pos)
			add_status_str(buf, size, posp, "Bot");
		else
			add_status_str(buf, size, posp, "All");
	} else if (pos == 0) {
		add_status_str(buf, size, posp, "Top");
	} else if (pos + h - 1 >= buffer->nl) {
		add_status_str(buf, size, posp, "Bot");
	} else {
		int d = buffer->nl - (h - 1);
		add_status_str(buf, size, posp, ssprintf("%2d%%", (pos * 100 + d / 2) / d));
	}
}

int format_status(char *buf, int size, const char *format, const char *misc_status)
{
	int pos = 0;
	int got_char;
	uchar u;

	separator = 0;
	got_char = buffer_get_char(&view->cursor, &u);
	if (got_char)
		u &= ~U_INVALID_MASK;
	while (pos < size && *format) {
		char ch = *format++;
		if (ch != '%') {
			if (separator)
				buf[pos++] = ' ';
			if (pos < size - 1)
				buf[pos++] = ch;
			separator = 0;
		} else {
			ch = *format++;
			switch (ch) {
			case 'f':
				add_status_str(buf, size, &pos,
						buffer->filename ? buffer->filename : "(No name)");
				break;
			case 'm':
				if (buffer_modified(buffer))
					add_status_str(buf, size, &pos, "*");
				break;
			case 'r':
				if (buffer->ro)
					add_status_str(buf, size, &pos, "RO");
				break;
			case 'y':
				add_status_str(buf, size, &pos, ssprintf("%d", view->cy + 1));
				break;
			case 'x':
				add_status_str(buf, size, &pos, ssprintf("%d", view->cx_display + 1));
				break;
			case 'X':
				add_status_str(buf, size, &pos, ssprintf("%d", view->cx_char + 1));
				if (view->cx_display != view->cx_char)
					add_status_str(buf, size, &pos, ssprintf("-%d", view->cx_display + 1));
				break;
			case 'c':
				if (got_char)
					add_status_str(buf, size, &pos, ssprintf("%3d", u));
				break;
			case 'C':
				if (got_char)
					add_status_str(buf, size, &pos, ssprintf("0x%02x", u));
				break;
			case 'p':
				add_status_pos(buf, size, &pos);
				break;
			case 'E':
				add_status_str(buf, size, &pos, buffer->utf8 ? "UTF-8" : "8-bit");
				break;
			case 'M':
				if (misc_status[0])
					add_status_str(buf, size, &pos, misc_status);
				break;
			case 'n':
				switch (buffer->newline) {
				case NEWLINE_UNIX:
					add_status_str(buf, size, &pos, "LF");
					break;
				case NEWLINE_DOS:
					add_status_str(buf, size, &pos, "CRLF");
					break;
				}
				break;
			case 's':
				separator = 1;
				break;
			case 't':
				add_status_str(buf, size, &pos, buffer->options.filetype);
				break;
			case '%':
				if (separator)
					buf[pos++] = ' ';
				if (pos < size - 1)
					buf[pos++] = ch;
				separator = 0;
				break;
			}
		}
	}
	buf[pos] = 0;
	return pos;
}
