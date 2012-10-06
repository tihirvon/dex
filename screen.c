#include "screen.h"
#include "format-status.h"
#include "window.h"
#include "editor.h"
#include "tabbar.h"
#include "obuf.h"
#include "cmdline.h"
#include "search.h"
#include "color.h"
#include "uchar.h"
#include "hl.h"
#include "frame.h"
#include "selection.h"
#include "git-open.h"
#include "path.h"

struct line_info {
	const unsigned char *line;
	unsigned int size;
	unsigned int pos;
	unsigned int indent_size;
	unsigned int trailing_ws_offset;
	struct hl_color **colors;
};

static int current_line;

static void set_color(struct term_color *color)
{
	struct term_color tmp = *color;

	// NOTE: -2 (keep) is treated as -1 (default)
	if (tmp.fg  < 0)
		tmp.fg = builtin_colors[BC_DEFAULT]->fg;
	if (tmp.bg  < 0)
		tmp.bg = builtin_colors[BC_DEFAULT]->bg;
	buf_set_color(&tmp);
}

static void set_builtin_color(enum builtin_color c)
{
	set_color(builtin_colors[c]);
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
		unsigned int i = strlen(buf);
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

static int get_char_width(unsigned int *idx)
{
	if (term_utf8) {
		return u_char_width(u_get_char(cmdline.buf.buffer, cmdline.buf.len, idx));
	} else {
		int i = *idx;
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
	unsigned int i, w;
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
			} else if (!strcmp(term, "screen")) {
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

// selection start / end buffer byte offsets
static unsigned int sel_so, sel_eo;
static unsigned int cur_offset;

static int is_default_bg_color(int color)
{
	return color == builtin_colors[BC_DEFAULT]->bg || color < 0;
}

static void mask_color(struct term_color *color, const struct term_color *over)
{
	if (over->fg != -2)
		color->fg = over->fg;
	if (over->bg != -2)
		color->bg = over->bg;
	if (!(over->attr & ATTR_KEEP))
		color->attr = over->attr;
}

// like mask_color() but can change bg color only if it has not been changed yet
static void mask_color2(struct term_color *color, const struct term_color *over)
{
	if (over->fg != -2)
		color->fg = over->fg;
	if (over->bg != -2 && is_default_bg_color(color->bg))
		color->bg = over->bg;
	if (!(over->attr & ATTR_KEEP))
		color->attr = over->attr;
}

static void mask_selection_and_current_line(struct term_color *color)
{
	if (selecting() && cur_offset >= sel_so && cur_offset < sel_eo) {
		mask_color(color, builtin_colors[BC_SELECTION]);
	} else if (current_line == view->cy) {
		mask_color2(color, builtin_colors[BC_CURRENTLINE]);
	}
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
	struct term_color color;
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

	if (info->colors && info->colors[pos]) {
		color = info->colors[pos]->color;
	} else {
		color = *builtin_colors[BC_DEFAULT];
	}
	if (is_non_text(u))
		mask_color(&color, builtin_colors[BC_NONTEXT]);
	if (ws_error)
		mask_color(&color, builtin_colors[BC_WSERROR]);
	mask_selection_and_current_line(&color);
	set_color(&color);

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

static int is_notice(const char *word, int len)
{
	static const char * const words[] = { "fixme", "todo", "xxx" };
	int i;

	for (i = 0; i < ARRAY_COUNT(words); i++) {
		const char *w = words[i];
		if (strlen(w) == len && !strncasecmp(w, word, len))
			return 1;
	}
	return 0;
}

// highlight certain words inside comments
static void hl_words(struct line_info *info)
{
	struct hl_color *cc = find_color("comment");
	struct hl_color *nc = find_color("notice");
	int i, j, si, max;

	if (info->colors == NULL || cc == NULL || nc == NULL)
		return;

	i = info->pos;
	if (i >= info->size)
		return;

	// go to beginning of partially visible word inside comment
	while (i > 0 && info->colors[i] == cc && is_word_byte(info->line[i]))
		i--;

	// This should be more than enough. I'm too lazy to iterate characters
	// instead of bytes and calculate text width.
	max = info->pos + screen_w * 4 + 8;

	while (i < info->size) {
		if (info->colors[i] != cc || !is_word_byte(info->line[i])) {
			if (i > max)
				break;
			i++;
		} else {
			// beginning of a word inside comment
			si = i++;
			while (i < info->size && info->colors[i] == cc && is_word_byte(info->line[i]))
				i++;
			if (is_notice(info->line + si, i - si)) {
				for (j = si; j < i; j++)
					info->colors[j] = nc;
			}
		}
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
	struct term_color color;
	unsigned int u;

	// Screen might be scrolled horizontally. Skip most invisible
	// characters using screen_skip_char() which is much faster than
	// buf_skip(screen_next_char(info)).
	//
	// There can be a wide character (tab, control code etc.) which is
	// partially visible and can't be skipped using screen_skip_char().
	while (obuf.x + 8 < obuf.scroll_x && info->pos < info->size)
		screen_skip_char(info);

	hl_words(info);

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
		// syntax highlighter highlights \n but use default color anyway
		color = *builtin_colors[BC_DEFAULT];
		mask_color(&color, builtin_colors[BC_NONTEXT]);
		mask_selection_and_current_line(&color);
		set_color(&color);
		buf_put_char('$');
	}

	color = *builtin_colors[BC_DEFAULT];
	mask_selection_and_current_line(&color);
	set_color(&color);
	cur_offset++;
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
		// dummy empty line is shown only if cursor is on it
		struct term_color color = *builtin_colors[BC_DEFAULT];

		obuf.x = 0;
		mask_color2(&color, builtin_colors[BC_CURRENTLINE]);
		set_color(&color);

		buf_move_cursor(window->edit_x, window->edit_y + i++);
		buf_clear_eol();
	}

	if (i < y2)
		set_builtin_color(BC_NOLINE);
	for (; i < y2; i++) {
		obuf.x = 0;
		buf_move_cursor(window->edit_x, window->edit_y + i);
		buf_put_char('~');
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

void update_line_numbers(struct window *win, int force)
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
