#include "screen.h"
#include "format-status.h"
#include "editor.h"
#include "tabbar.h"
#include "obuf.h"
#include "cmdline.h"
#include "search.h"
#include "uchar.h"
#include "frame.h"
#include "git-open.h"
#include "path.h"

void set_color(struct term_color *color)
{
	struct term_color tmp = *color;

	// NOTE: -2 (keep) is treated as -1 (default)
	if (tmp.fg  < 0)
		tmp.fg = builtin_colors[BC_DEFAULT]->fg;
	if (tmp.bg  < 0)
		tmp.bg = builtin_colors[BC_DEFAULT]->bg;
	buf_set_color(&tmp);
}

void set_builtin_color(enum builtin_color c)
{
	set_color(builtin_colors[c]);
}

static unsigned int term_get_char(const unsigned char *buf, long size, long *idx)
{
	long i = *idx;
	unsigned int u;

	if (term_utf8) {
		u = u_get_char(buf, size, &i);
	} else {
		u = buf[i++];
	}
	*idx = i;
	return u;
}

static void print_horizontal_tab_title(struct view *v, int idx)
{
	int skip = v->tt_width - v->tt_truncated_width;
	const char *filename = buffer_filename(v->buffer);
	char buf[16];

	if (skip > 0) {
		filename += u_skip_chars(filename, &skip);
	}

	snprintf(buf, sizeof(buf), "%c%d%s",
		obuf.x == 0 && idx > 0 ? '<' : ' ',
		idx + 1,
		buffer_modified(v->buffer) ? "+" : ":");

	if (v == view)
		set_builtin_color(BC_ACTIVETAB);
	else
		set_builtin_color(BC_INACTIVETAB);
	buf_add_str(buf);
	buf_add_str(filename);
	if (obuf.x == obuf.width - 1 && idx < window->views.count - 1)
		buf_put_char('>');
	else
		buf_put_char(' ');
}

static void print_horizontal_tabbar(void)
{
	int i;

	buf_reset(window->x, window->w, 0);
	buf_move_cursor(window->x, window->y);

	calculate_tabbar(window);
	for (i = window->first_tab_idx; i < window->views.count; i++) {
		struct view *v = window->views.ptrs[i];

		if (obuf.x + v->tt_truncated_width > window->w)
			break;
		print_horizontal_tab_title(v, i);
	}
	set_builtin_color(BC_TABBAR);
	if (i != window->views.count) {
		while (obuf.x < obuf.width - 1)
			buf_put_char(' ');
		if (obuf.x == obuf.width - 1)
			buf_put_char('>');
	} else {
		buf_clear_eol();
	}
}

static void print_vertical_tab_title(struct view *v, int idx, int width)
{
	const char *orig_filename = buffer_filename(v->buffer);
	const char *filename = orig_filename;
	int max = options.tab_bar_max_components;
	char buf[16];
	int skip;

	snprintf(buf, sizeof(buf), "%2d%s", idx + 1, buffer_modified(v->buffer) ? "+" : " ");
	if (max) {
		int i, count = 1;

		for (i = 0; filename[i]; i++) {
			if (filename[i] == '/')
				count++;
		}
		// ignore first slash because it does not separate components
		if (filename[0] == '/')
			count--;

		if (count > max) {
			// skip possible first slash
			for (i = 1; ; i++) {
				if (filename[i] == '/' && --count == max) {
					i++;
					break;
				}
			}
			filename += i;
		}
	} else {
		skip = strlen(buf) + u_str_width(filename) - width + 1;
		if (skip > 0)
			filename += u_skip_chars(filename, &skip);
	}
	if (filename != orig_filename) {
		// filename was shortened. add "<<" symbol
		long i = strlen(buf);
		u_set_char(buf, &i, 0xab);
		buf[i] = 0;
	}

	if (v == view)
		set_builtin_color(BC_ACTIVETAB);
	else
		set_builtin_color(BC_INACTIVETAB);
	buf_add_str(buf);
	buf_add_str(filename);
	buf_clear_eol();
}

static void print_vertical_tabbar(void)
{
	int width = vertical_tabbar_width(window);
	int h = window->edit_h;
	int i, n, cur_idx = 0;

	for (i = 0; i < window->views.count; i++) {
		if (view == window->views.ptrs[i]) {
			cur_idx = i;
			break;
		}
	}
	if (window->views.count <= h) {
		// all tabs fit
		window->first_tab_idx = 0;
	} else {
		int max_y = window->first_tab_idx + h - 1;

		if (window->first_tab_idx > cur_idx)
			window->first_tab_idx = cur_idx;
		if (cur_idx > max_y)
			window->first_tab_idx += cur_idx - max_y;
	}

	buf_reset(window->x, width, 0);
	n = h;
	if (n + window->first_tab_idx > window->views.count)
		n = window->views.count - window->first_tab_idx;
	for (i = 0; i < n; i++) {
		int idx = window->first_tab_idx + i;
		obuf.x = 0;
		buf_move_cursor(window->x, window->y + i);
		print_vertical_tab_title(window->views.ptrs[idx], idx, width);
	}
	set_builtin_color(BC_TABBAR);
	for (; i < h; i++) {
		obuf.x = 0;
		buf_move_cursor(window->x, window->y + i);
		buf_clear_eol();
	}
}

void print_tabbar(void)
{
	if (options.vertical_tab_bar) {
		print_vertical_tabbar();
	} else {
		print_horizontal_tabbar();
	}
}

void update_status_line(void)
{
	char lbuf[256];
	char rbuf[256];
	int lw, rw;

	buf_reset(window->x, window->w, 0);
	buf_move_cursor(window->x, window->y + window->h - 1);
	set_builtin_color(BC_STATUSLINE);
	format_status(lbuf, sizeof(lbuf), options.statusline_left);
	format_status(rbuf, sizeof(rbuf), options.statusline_right);
	lw = u_str_width(lbuf);
	rw = u_str_width(rbuf);
	if (lw + rw <= window->w) {
		// both fit
		buf_add_str(lbuf);
		buf_set_bytes(' ', window->w - lw - rw);
		buf_add_str(rbuf);
	} else if (lw <= window->w && rw <= window->w) {
		// both would fit separately, draw overlapping
		buf_add_str(lbuf);
		obuf.x = window->w - rw;
		buf_move_cursor(window->x + window->w - rw, window->y + window->h - 1);
		buf_add_str(rbuf);
	} else if (lw <= window->w) {
		// left fits
		buf_add_str(lbuf);
		buf_clear_eol();
	} else if (rw <= window->w) {
		// right fits
		buf_set_bytes(' ', window->w - rw);
		buf_add_str(rbuf);
	} else {
		buf_clear_eol();
	}
}

static int get_char_width(long *idx)
{
	if (term_utf8) {
		return u_char_width(u_get_char(cmdline.buf.buffer, cmdline.buf.len, idx));
	} else {
		long i = *idx;
		unsigned char ch = cmdline.buf.buffer[i++];

		*idx = i;
		if (u_is_ctrl(ch))
			return 2;
		if (ch >= 0x80 && ch <= 0x9f)
			return 4;
		return 1;
	}
}

int print_command(char prefix)
{
	long i, w;
	unsigned int u;
	int x;

	// width of characters up to and including cursor position
	w = 1; // ":" (prefix)
	i = 0;
	while (i <= cmdline.pos && cmdline.buf.buffer[i])
		w += get_char_width(&i);
	if (!cmdline.buf.buffer[cmdline.pos])
		w++;

	if (w > screen_w)
		obuf.scroll_x = w - screen_w;

	set_builtin_color(BC_COMMANDLINE);
	i = 0;
	buf_put_char(prefix);
	x = obuf.x - obuf.scroll_x;
	while (cmdline.buf.buffer[i]) {
		BUG_ON(obuf.x > obuf.scroll_x + obuf.width);
		u = term_get_char(cmdline.buf.buffer, cmdline.buf.len, &i);
		if (!buf_put_char(u))
			break;
		if (i <= cmdline.pos)
			x = obuf.x - obuf.scroll_x;
	}
	return x;
}

void print_message(const char *msg, bool is_error)
{
	enum builtin_color c = BC_COMMANDLINE;
	long i = 0;

	if (msg[0])
		c = is_error ? BC_ERRORMSG : BC_INFOMSG;
	set_builtin_color(c);
	while (msg[i]) {
		unsigned int u = term_get_char(msg, i + 4, &i);
		if (!buf_put_char(u))
			break;
	}
}

void update_term_title(void)
{
	static int term_type = -1;
	char title[1024];

	if (term_type == -1) {
		const char *term = getenv("TERM");

		term_type = 0;
		if (term) {
			if (strstr(term, "xterm") || strstr(term, "rxvt")) {
				term_type = 1;
			} else if (streq(term, "screen")) {
				term_type = 2;
			}
		}
	}

	// FIXME: title must not contain control characters
	snprintf(title, sizeof(title), "%s %c %s",
		buffer_filename(buffer),
		buffer_modified(buffer) ? '+' : '-',
		program);

	switch (term_type) {
	case 1:
		// xterm or compatible
		buf_escape("\033]2;");
		buf_escape(title);
		buf_escape("\007");
		break;
	case 2:
		// tmux or screen
		// NOTE: screen might need to be configured to get it working
		buf_escape("\033_");
		buf_escape(title);
		buf_escape("\033\\");
		break;
	}
}

void mask_color(struct term_color *color, const struct term_color *over)
{
	if (over->fg != -2)
		color->fg = over->fg;
	if (over->bg != -2)
		color->bg = over->bg;
	if (!(over->attr & ATTR_KEEP))
		color->attr = over->attr;
}

static void print_separator(struct window *win)
{
	int y;

	if (win->x + win->w == screen_w)
		return;

	for (y = 0; y < win->h; y++) {
		buf_move_cursor(win->x + win->w, win->y + y);
		buf_add_ch('|');
	}
}

void update_separators(void)
{
	int i;

	set_builtin_color(BC_STATUSLINE);
	for (i = 0; i < windows.count; i++)
		print_separator(windows.ptrs[i]);
}

void update_line_numbers(struct window *win, bool force)
{
	struct view *v = win->view;
	int i, first, last;
	int x = win->x + vertical_tabbar_width(win);

	calculate_line_numbers(win);

	first = v->vy + 1;
	last = v->vy + win->edit_h;
	if (last > v->buffer->nl)
		last = v->buffer->nl;

	if (!force && win->line_numbers.first == first && win->line_numbers.last == last)
		return;

	win->line_numbers.first = first;
	win->line_numbers.last = last;

	buf_reset(win->x, win->w, 0);
	set_builtin_color(BC_LINENUMBER);
	for (i = 0; i < win->edit_h; i++) {
		int line = v->vy + i + 1;
		int w = win->line_numbers.width - 1;
		char buf[32];

		if (line > buffer->nl) {
			snprintf(buf, sizeof(buf), "%*s ", w, "");
		} else {
			snprintf(buf, sizeof(buf), "%*d ", w, line);
		}
		buf_move_cursor(x, win->edit_y + i);
		buf_add_bytes(buf, win->line_numbers.width);
	}
}

void update_git_open(void)
{
	int x = 0;
	int y = 0;
	int w = screen_w;
	int h = screen_h - 1;
	int max_y = git_open.scroll + h - 1;
	int i;

	if (h >= git_open.files.count)
		git_open.scroll = 0;
	if (git_open.scroll > git_open.selected)
		git_open.scroll = git_open.selected;
	if (git_open.selected > max_y)
		git_open.scroll += git_open.selected - max_y;

	buf_reset(x, w, 0);
	buf_move_cursor(0, 0);
	cmdline_x = print_command('/');
	buf_clear_eol();
	y++;

	for (i = 0; i < h; i++) {
		int file_idx = git_open.scroll + i;
		char *file;
		struct term_color color;

		if (file_idx >= git_open.files.count)
			break;

		file = git_open.files.ptrs[file_idx];
		obuf.x = 0;
		buf_move_cursor(x, y + i);

		color = *builtin_colors[BC_DEFAULT];
		if (file_idx == git_open.selected)
			mask_color(&color, builtin_colors[BC_SELECTION]);
		buf_set_color(&color);
		if (term_utf8) {
			buf_add_str(file);
		} else {
			char *tmp = filename_to_utf8(file);
			buf_add_str(tmp);
			free(tmp);
		}
		buf_clear_eol();
	}
	set_builtin_color(BC_DEFAULT);
	for (; i < h; i++) {
		obuf.x = 0;
		buf_move_cursor(x, y + i);
		buf_clear_eol();
	}
}

void update_window_sizes(void)
{
	set_frame_size(root_frame, screen_w, screen_h - 1);
	update_window_coordinates();
}

void update_screen_size(void)
{
	if (!term_get_size(&screen_w, &screen_h)) {
		if (screen_w < 3)
			screen_w = 3;
		if (screen_h < 3)
			screen_h = 3;
		update_window_sizes();
	}
}
