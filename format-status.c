#include "format-status.h"
#include "window.h"
#include "uchar.h"
#include "editor.h"
#include "input-special.h"
#include "selection.h"

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
	while (f->pos < f->size && str[idx])
		u_set_char(f->buf, &f->pos, u_get_char(str, idx + 4, &idx));
}

FORMAT(2)
static void add_status_format(struct formatter *f, const char *format, ...)
{
	char buf[1024];
	va_list ap;

	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	add_status_str(f, buf);
}

static void add_status_pos(struct formatter *f)
{
	int h = window->edit_h;
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
		add_status_format(f, "%2d%%", (pos * 100 + d / 2) / d);
	}
}

static const char *format_misc_status(void)
{
	static char misc_status[32];

	if (input_special) {
		format_input_special_misc_status(misc_status);
	} else if (input_mode == INPUT_SEARCH) {
		snprintf(misc_status, sizeof(misc_status), "[case-sensitive = %s]",
			case_sensitive_search_enum[options.case_sensitive_search]);
	} else if (selecting()) {
		struct selection_info info;

		init_selection(&info);
		if (view->selection == SELECT_LINES) {
			snprintf(misc_status, sizeof(misc_status), "[%d lines]", get_nr_selected_lines(&info));
		} else {
			snprintf(misc_status, sizeof(misc_status), "[%d chars]", get_nr_selected_chars(&info));
		}
	} else {
		misc_status[0] = 0;
	}
	return misc_status;
}

int format_status(char *buf, int size, const char *format)
{
	struct formatter f;
	int got_char;
	unsigned int u;

	f.buf = buf;
	f.size = size - 5; // max length of char and terminating NUL
	f.pos = 0;
	f.separator = 0;

	got_char = buffer_get_char(&view->cursor, &u);
	while (f.pos < f.size && *format) {
		char ch = *format++;
		if (ch != '%') {
			add_separator(&f);
			add_ch(&f, ch);
		} else {
			ch = *format++;
			switch (ch) {
			case 'f':
				add_status_str(&f, buffer_filename(buffer));
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
				add_status_format(&f, "%d", view->cy + 1);
				break;
			case 'x':
				add_status_format(&f, "%d", view->cx_display + 1);
				break;
			case 'X':
				add_status_format(&f, "%d", view->cx_char + 1);
				if (view->cx_display != view->cx_char)
					add_status_format(&f, "-%d", view->cx_display + 1);
				break;
			case 'p':
				add_status_pos(&f);
				break;
			case 'E':
				add_status_str(&f, buffer->encoding);
				break;
			case 'M': {
				const char *misc_status = format_misc_status();
				if (misc_status[0])
					add_status_str(&f, misc_status);
				break;
			}
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
			case 'u':
				if (got_char) {
					if (u_is_unicode(u)) {
						add_status_format(&f, "U+%04X", u);
					} else {
						add_status_str(&f, "Invalid");
					}
				}
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
