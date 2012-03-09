#include "screen.h"
#include "format-status.h"
#include "window.h"
#include "tabbar.h"
#include "obuf.h"
#include "cmdline.h"
#include "search.h"
#include "color.h"
#include "uchar.h"
#include "hl.h"
#include "frame.h"
#include "selection.h"

struct line_info {
	const unsigned char *line;
	unsigned int size;
	unsigned int pos;
	unsigned int indent_size;
	unsigned int trailing_ws_offset;
	struct hl_color **colors;
};

enum builtin_color {
	BC_DEFAULT,
	BC_NONTEXT,
	BC_WSERROR,
	BC_SELECTION,
	BC_CURRENTLINE,
	BC_LINENUMBER,
	BC_STATUSLINE,
	BC_COMMANDLINE,
	BC_ERRORMSG,
	BC_INFOMSG,
	BC_TABBAR,
	BC_ACTIVETAB,
	BC_INACTIVETAB,
	NR_BC
};

static const char * const builtin_color_names[NR_BC] = {
	"default",
	"nontext",
	"wserror",
	"selection",
	"currentline",
	"linenumber",
	"statusline",
	"commandline",
	"errormsg",
	"infomsg",
	"tabbar",
	"activetab",
	"inactivetab",
};

static struct term_color *builtin_colors[NR_BC];
static int current_line;

void set_basic_colors(void)
{
	int i;
	for (i = 0; i < NR_BC; i++)
		builtin_colors[i] = &find_color(builtin_color_names[i])->color;
}

static void set_builtin_color(enum builtin_color c)
{
	buf_set_color(builtin_colors[c]);
}

static unsigned int term_get_char(const char *buf, unsigned int size, unsigned int *idx)
{
	unsigned int i = *idx;
	unsigned int u;

	if (term_utf8) {
		u = u_get_char(buf, size, &i);
	} else {
		u = buf[i++];
	}
	*idx = i;
	return u;
}

static void print_tab_title(struct view *v, int idx)
{
	int skip = v->tt_width - v->tt_truncated_width;
	const char *filename = buffer_filename(v->buffer);
	char buf[16];

	if (skip > 0) {
		if (term_utf8)
			filename += u_skip_chars(filename, &skip);
		else
			filename += skip;
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

	if (term_utf8) {
		unsigned int si = 0;
		while (filename[si])
			buf_put_char(u_get_char(filename, si + 4, &si));
	} else {
		unsigned int si = 0;
		while (filename[si])
			buf_put_char(filename[si++]);
	}
	if (obuf.x == obuf.width - 1 && idx < window->views.count - 1)
		buf_ch('>');
	else
		buf_ch(' ');
}

void print_tabbar(void)
{
	int idx;

	buf_reset(window->x, window->w, 0);
	buf_move_cursor(window->x, window->y);

	calculate_tabbar(window);
	for (idx = 0; idx < window->views.count; idx++) {
		struct view *v = window->views.ptrs[idx];

		if (idx < window->first_tab_idx)
			continue;

		if (obuf.x + v->tt_truncated_width > window->w)
			break;

		print_tab_title(v, idx);
	}
	set_builtin_color(BC_TABBAR);
	if (idx != window->views.count) {
		while (obuf.x < obuf.width - 1)
			buf_ch(' ');
		if (obuf.x == obuf.width - 1)
			buf_ch('>');
	} else {
		buf_clear_eol();
	}
}

void update_status_line(const char *misc_status)
{
	char lbuf[256];
	char rbuf[256];
	int lw, rw;

	buf_reset(window->x, window->w, 0);
	buf_move_cursor(window->x, window->y + window->h - 1);
	set_builtin_color(BC_STATUSLINE);
	lw = format_status(lbuf, sizeof(lbuf), options.statusline_left, misc_status);
	rw = format_status(rbuf, sizeof(rbuf), options.statusline_right, misc_status);
	if (term_utf8) {
		lw = u_str_width(lbuf, lw);
		rw = u_str_width(rbuf, rw);
	}
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

static int get_char_width(unsigned int *idx)
{
	if (term_utf8) {
		return u_char_width(u_get_char(cmdline.buffer, cmdline.len, idx));
	} else {
		int i = *idx;
		unsigned char ch = cmdline.buffer[i++];

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
	unsigned int i, w;
	unsigned int u;
	int x;

	// width of characters up to and including cursor position
	w = 1; // ":" (prefix)
	i = 0;
	while (i <= cmdline_pos && cmdline.buffer[i])
		w += get_char_width(&i);
	if (!cmdline.buffer[cmdline_pos])
		w++;

	if (w > screen_w)
		obuf.scroll_x = w - screen_w;

	set_builtin_color(BC_COMMANDLINE);
	i = 0;
	if (obuf.x < obuf.scroll_x) {
		buf_skip(prefix);
		while (obuf.x < obuf.scroll_x && cmdline.buffer[i]) {
			u = term_get_char(cmdline.buffer, cmdline.len, &i);
			buf_skip(u);
		}
	} else {
		buf_put_char(prefix);
	}

	x = obuf.x - obuf.scroll_x;
	while (cmdline.buffer[i]) {
		BUG_ON(obuf.x > obuf.scroll_x + obuf.width);
		u = term_get_char(cmdline.buffer, cmdline.len, &i);
		if (!buf_put_char(u))
			break;
		if (i <= cmdline_pos)
			x = obuf.x - obuf.scroll_x;
	}
	return x;
}

void print_message(const char *msg, int is_error)
{
	enum builtin_color c = BC_COMMANDLINE;
	unsigned int i = 0;

	if (msg[0])
		c = is_error ? BC_ERRORMSG : BC_INFOMSG;
	set_builtin_color(c);
	while (msg[i]) {
		unsigned int u = term_get_char(msg, i + 4, &i);
		if (!buf_put_char(u))
			break;
	}
}

static int term_title_supported(void)
{
	static int supported = -1;

	if (supported == -1) {
		const char *term = getenv("TERM");

		supported = term && (strstr(term, "xterm") || strstr(term, "rxvt"));
	}
	return supported;
}

// title must not contain control characters
void print_term_title(const char *title)
{
	if (!term_title_supported())
		return;

	buf_escape("\033]2;");
	buf_escape(title);
	buf_escape("\007");
}

// selection start / end buffer byte offsets
static unsigned int sel_so, sel_eo;
static unsigned int cur_offset;

static void mask_color(struct term_color *color, const struct term_color *over)
{
	if (over->fg != -2)
		color->fg = over->fg;
	if (over->bg != -2)
		color->bg = over->bg;
	if (!(over->attr & ATTR_KEEP))
		color->attr = over->attr;
}

static void update_color(struct hl_color *hl_color, int nontext, int wserror)
{
	struct term_color color;

	if (hl_color)
		color = hl_color->color;
	else
		color = *builtin_colors[BC_DEFAULT];

	if (nontext)
		mask_color(&color, builtin_colors[BC_NONTEXT]);
	if (wserror)
		mask_color(&color, builtin_colors[BC_WSERROR]);
	if (selecting() && cur_offset >= sel_so && cur_offset < sel_eo)
		mask_color(&color, builtin_colors[BC_SELECTION]);
	else if (current_line == view->cy)
		mask_color(&color, builtin_colors[BC_CURRENTLINE]);
	buf_set_color(&color);
}

static void selection_init(void)
{
	struct selection_info info;

	if (!selecting())
		return;

	if (view->sel_eo != UINT_MAX) {
		/* already calculated */
		sel_so = view->sel_so;
		sel_eo = view->sel_eo;
		BUG_ON(sel_so > sel_eo);
		return;
	}

	init_selection(&info);
	sel_so = info.so;
	sel_eo = info.eo;
}

static int is_non_text(unsigned int u)
{
	if (u < 0x20)
		return u != '\t' || options.display_special;
	if (u == 0x7f)
		return 1;
	return u_is_unprintable(u);
}

static int whitespace_error(struct line_info *info, unsigned int u, unsigned int i)
{
	int flags = buffer->options.ws_error;

	if (i >= info->trailing_ws_offset && flags & WSE_TRAILING) {
		// Trailing whitespace.
		if (current_line != view->cy || view->cx < info->trailing_ws_offset)
			return 1;
		// Cursor is on this line and on the whitespace or at eol. It would
		// be annoying if the line you are editing displays trailing
		// whitespace as an error.
	}

	if (u == '\t') {
		if (i < info->indent_size) {
			// in indentation
			if (flags & WSE_TAB_INDENT)
				return 1;
		} else {
			if (flags & WSE_TAB_AFTER_INDENT)
				return 1;
		}
	} else if (i < info->indent_size) {
		// space in indentation
		const char *line = info->line;
		int count = 0, pos = i;

		while (pos > 0 && line[pos - 1] == ' ')
			pos--;
		while (pos < info->size && line[pos] == ' ') {
			pos++;
			count++;
		}

		if (count >= buffer->options.tab_width) {
			// spaces used instead of tab
			if (flags & WSE_SPACE_INDENT)
				return 1;
		} else if (pos < info->size && line[pos] == '\t') {
			// space before tab
			if (flags & WSE_SPACE_INDENT)
				return 1;
		} else {
			// less than tab width spaces at end of indentation
			if (flags & WSE_SPACE_ALIGN)
				return 1;
		}
	}
	return 0;
}

static unsigned int screen_next_char(struct line_info *info)
{
	unsigned int count, pos = info->pos;
	unsigned int u = info->line[pos];
	int ws_error = 0;

	if (likely(u < 0x80)) {
		info->pos++;
		count = 1;
		if (u == '\t' || u == ' ')
			ws_error = whitespace_error(info, u, pos);
	} else {
		u = u_get_nonascii(info->line, info->size, &info->pos);
		count = info->pos - pos;

		// highly annoying no-break space etc.?
		if (u_is_special_whitespace(u) && (buffer->options.ws_error & WSE_SPECIAL))
			ws_error = 1;
	}

	update_color(info->colors ? info->colors[pos] : NULL, is_non_text(u), ws_error);
	cur_offset += count;
	return u;
}

static void screen_skip_char(struct line_info *info)
{
	unsigned int u = info->line[info->pos++];

	cur_offset++;
	if (likely(u < 0x80)) {
		if (likely(!u_is_ctrl(u))) {
			obuf.x++;
		} else if (u == '\t' && obuf.tab != TAB_CONTROL) {
			obuf.x += (obuf.x + obuf.tab_width) / obuf.tab_width * obuf.tab_width - obuf.x;
		} else {
			// control
			obuf.x += 2;
		}
	} else {
		unsigned int pos = info->pos;

		info->pos--;
		u = u_get_nonascii(info->line, info->size, &info->pos);
		obuf.x += u_char_width(u);
		cur_offset += info->pos - pos;
	}
}

static void init_line_info(struct line_info *info, struct lineref *lr, struct hl_color **colors)
{
	int i;

	BUG_ON(lr->size == 0);
	BUG_ON(lr->line[lr->size - 1] != '\n');

	info->line = lr->line;
	info->size = lr->size - 1;
	info->pos = 0;
	info->colors = colors;

	for (i = 0; i < info->size; i++) {
		char ch = info->line[i];
		if (ch != '\t' && ch != ' ')
			break;
	}
	info->indent_size = i;

	info->trailing_ws_offset = INT_MAX;
	for (i = info->size - 1; i >= 0; i--) {
		char ch = info->line[i];
		if (ch != '\t' && ch != ' ')
			break;
		info->trailing_ws_offset = i;
	}
}

static void print_line(struct line_info *info)
{
	unsigned int u;

	/*
	 * Skip most characters using screen_skip_char() which is much
	 * faster than screen_next_char() which does color updating etc.
	 */
	while (obuf.x + 8 < obuf.scroll_x && info->pos < info->size)
		screen_skip_char(info);

	/*
	 * Skip rest. If a skipped character is wide (tab, control code
	 * etc.) and we need to display part of it then we must update
	 * color before calling buf_skip().
	 */
	while (obuf.x < obuf.scroll_x && info->pos < info->size) {
		u = screen_next_char(info);
		buf_skip(u);
	}

	/*
	 * Fully visible characters (except possibly the last one).
	 */
	while (info->pos < info->size) {
		BUG_ON(obuf.x > obuf.scroll_x + obuf.width);
		u = screen_next_char(info);
		if (!buf_put_char(u)) {
			// +1 for newline
			cur_offset += info->size - info->pos + 1;
			return;
		}
	}

	if (options.display_special && obuf.x >= obuf.scroll_x) {
		update_color(info->colors ? info->colors[info->pos] : NULL, 1, 0);
		buf_put_char('$');
	}
	update_color(NULL, 0, 0);
	cur_offset++; // must be after update_color()
	buf_clear_eol();
}

void update_range(int y1, int y2)
{
	struct block_iter bi = view->cursor;
	int i, got_line;

	buf_reset(window->edit_x, window->edit_w, view->vx);
	obuf.tab_width = buffer->options.tab_width;
	obuf.tab = options.display_special ? TAB_SPECIAL : TAB_NORMAL;

	for (i = 0; i < view->cy - y1; i++)
		block_iter_prev_line(&bi);
	for (i = 0; i < y1 - view->cy; i++)
		block_iter_eat_line(&bi);
	block_iter_bol(&bi);

	current_line = y1;
	y1 -= view->vy;
	y2 -= view->vy;

	cur_offset = block_iter_get_offset(&bi);
	selection_init();

	got_line = !block_iter_is_eof(&bi);
	hl_fill_start_states(current_line);
	for (i = y1; got_line && i < y2; i++) {
		struct line_info info;
		struct lineref lr;
		struct hl_color **colors;
		int next_changed;

		obuf.x = 0;
		buf_move_cursor(window->edit_x, window->edit_y + i);

		fill_line_nl_ref(&bi, &lr);
		colors = hl_line(lr.line, lr.size, current_line, &next_changed);
		init_line_info(&info, &lr, colors);
		print_line(&info);

		got_line = block_iter_next_line(&bi);
		current_line++;

		if (next_changed && i + 1 == y2 && y2 < window->edit_h) {
			// more lines need to be updated not because their
			// contents have changed but because their highlight
			// state has
			y2++;
		}
	}

	if (i < y2 && current_line == view->cy) {
		// dummy empty line
		obuf.x = 0;
		update_color(NULL, 0, 0);
		buf_move_cursor(window->edit_x, window->edit_y + i++);
		buf_clear_eol();
	}

	if (i < y2)
		set_builtin_color(BC_NONTEXT);
	for (; i < y2; i++) {
		obuf.x = 0;
		buf_move_cursor(window->edit_x, window->edit_y + i);
		buf_ch('~');
		buf_clear_eol();
	}
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

static const char *format_line_number(int line, int w)
{
	if (line > buffer->nl)
		return ssprintf("%*s ", w - 1, "");
	return ssprintf("%*d ", w - 1, line);
}

void update_line_numbers(struct window *win, int force)
{
	struct view *v = win->view;
	int i, first, last;

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
		const char *buf = format_line_number(v->vy + i + 1, win->line_numbers.width);
		buf_move_cursor(win->x, win->edit_y + i);
		buf_add_bytes(buf, win->line_numbers.width);
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
