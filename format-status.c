#include "format-status.h"
#include "window.h"
#include "uchar.h"

struct formatter {
	char *buf;
	unsigned int size;
	unsigned int pos;
	int separator;
};

static void add_ch(struct formatter *f, char ch)
{
	f->buf[f->pos++] = ch;
}

static void add_separator(struct formatter *f)
{
	if (f->separator && f->pos < f->size)
		add_ch(f, ' ');
	f->separator = 0;
}

static void add_status_str(struct formatter *f, const char *str)
{
	unsigned int idx = 0;

	if (!*str)
		return;

	add_separator(f);
	if (term_utf8) {
		while (f->pos < f->size && str[idx]) {
			unsigned int u = u_buf_get_char(str, idx + 4, &idx);
			u_set_char(f->buf, &f->pos, u);
		}
	} else {
		while (f->pos < f->size && str[idx]) {
			unsigned char ch = str[idx++];
			if (u_is_ctrl(ch)) {
				u_set_ctrl(f->buf, &f->pos, ch);
			} else if (ch >= 0x80 && ch <= 0x9f) {
				u_set_hex(f->buf, &f->pos, ch);
			} else {
				add_ch(f, ch);
			}
		}
	}
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

static void add_status_pos(struct formatter *f)
{
	int h = window->h;
	int pos = view->vy;

	if (buffer->nl <= h) {
		if (pos)
			add_status_str(f, "Bot");
		else
			add_status_str(f, "All");
	} else if (pos == 0) {
		add_status_str(f, "Top");
	} else if (pos + h - 1 >= buffer->nl) {
		add_status_str(f, "Bot");
	} else {
		int d = buffer->nl - (h - 1);
		add_status_str(f, ssprintf("%2d%%", (pos * 100 + d / 2) / d));
	}
}

int format_status(char *buf, int size, const char *format, const char *misc_status)
{
	struct formatter f;
	int got_char;
	unsigned int u;

	f.buf = buf;
	f.size = size - 5; // max length of char and terminating NUL
	f.pos = 0;
	f.separator = 0;

	got_char = buffer_get_char(&view->cursor, &u);
	if (got_char)
		u &= ~U_INVALID_MASK;
	while (f.pos < f.size && *format) {
		char ch = *format++;
		if (ch != '%') {
			add_separator(&f);
			add_ch(&f, ch);
		} else {
			ch = *format++;
			switch (ch) {
			case 'f':
				add_status_str(&f, buffer->filename ? buffer->filename : "(No name)");
				break;
			case 'm':
				if (buffer_modified(buffer))
					add_status_str(&f, "*");
				break;
			case 'r':
				if (buffer->ro)
					add_status_str(&f, "RO");
				break;
			case 'y':
				add_status_str(&f, ssprintf("%d", view->cy + 1));
				break;
			case 'x':
				add_status_str(&f, ssprintf("%d", view->cx_display + 1));
				break;
			case 'X':
				add_status_str(&f, ssprintf("%d", view->cx_char + 1));
				if (view->cx_display != view->cx_char)
					add_status_str(&f, ssprintf("-%d", view->cx_display + 1));
				break;
			case 'c':
				if (got_char)
					add_status_str(&f, ssprintf("%3d", u));
				break;
			case 'C':
				if (got_char)
					add_status_str(&f, ssprintf("0x%02x", u));
				break;
			case 'p':
				add_status_pos(&f);
				break;
			case 'E':
				add_status_str(&f, buffer->options.utf8 ? "UTF-8" : "8-bit");
				break;
			case 'M':
				if (misc_status[0])
					add_status_str(&f, misc_status);
				break;
			case 'n':
				switch (buffer->newline) {
				case NEWLINE_UNIX:
					add_status_str(&f, "LF");
					break;
				case NEWLINE_DOS:
					add_status_str(&f, "CRLF");
					break;
				}
				break;
			case 's':
				f.separator = 1;
				break;
			case 't':
				add_status_str(&f, buffer->options.filetype);
				break;
			case '%':
				add_separator(&f);
				add_ch(&f, ch);
				break;
			}
		}
	}
	f.buf[f.pos] = 0;
	return f.pos;
}
