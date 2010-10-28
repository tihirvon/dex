#include "screen.h"
#include "format-status.h"
#include "window.h"
#include "obuf.h"
#include "cmdline.h"
#include "search.h"
#include "color.h"

struct line_info {
	const char *line;
	unsigned int size;

	unsigned int pos;
	unsigned int indent_size;
	unsigned int trailing_ws_offset;

	struct hl_color **colors;
};

static struct hl_color *default_color;
static struct hl_color *currentline_color;
static struct hl_color *selection_color;
static struct hl_color *statusline_color;
static struct hl_color *commandline_color;
static struct hl_color *errormsg_color;
static struct hl_color *infomsg_color;
static struct hl_color *wserror_color;
static struct hl_color *nontext_color;
static struct hl_color *tab_bar_color;
static struct hl_color *tab_active_color;
static struct hl_color *tab_inactive_color;

static int current_line;

static const char *no_name = "(No name)";

static uchar term_get_char(const char *buf, unsigned int size, unsigned int *idx)
{
	unsigned int i = *idx;
	uchar u;

	if (term_flags & TERM_UTF8) {
		u = u_buf_get_char(buf, size, &i);
	} else {
		u = buf[i++];
	}
	*idx = i;
	return u;
}

static unsigned int number_width(unsigned int n)
{
	unsigned int width = 0;

	do {
		n /= 10;
		width++;
	} while (n);
	return width;
}

static void update_tab_title_width(struct view *v, int idx)
{
	const char *filename = v->buffer->filename;
	unsigned int w;

	if (!filename)
		filename = no_name;

	w = 3 + number_width(idx + 1);
	if (term_flags & TERM_UTF8) {
		unsigned int i = 0;
		while (filename[i])
			w += u_char_width(u_buf_get_char(filename, i + 4, &i));
	} else {
		w += strlen(filename);
	}

	v->tt_width = w;
	v->tt_truncated_width = w;
}

static void print_tab_title(struct view *v, int idx, int skip)
{
	const char *filename = v->buffer->filename;
	char buf[16];

	if (!filename)
		filename = no_name;

	if (skip > 0) {
		if (term_flags & TERM_UTF8)
			filename += u_skip_chars(filename, &skip);
		else
			filename += skip;
	}

	snprintf(buf, sizeof(buf), " %d%s",
		idx + 1,
		buffer_modified(v->buffer) ? "+" : ":");
	buf_add_str(buf);

	if (term_flags & TERM_UTF8) {
		unsigned int si = 0;
		while (filename[si])
			buf_put_char(u_buf_get_char(filename, si + 4, &si), 1);
	} else {
		unsigned int si = 0;
		while (filename[si])
			buf_put_char(filename[si++], 0);
	}
	buf_ch(' ');
}

void print_tab_bar(void)
{
	/* index of left-most visible tab */
	static int left_idx;
	struct view *v;
	int trunc_min_w = 20;
	int count = 0, total_len = 0;
	int trunc_count = 0, max_trunc_w = 0;
	int idx;

	list_for_each_entry(v, &window->views, node) {
		if (v == view) {
			/* make sure current tab is visible */
			if (left_idx > count)
				left_idx = count;
			/* title of current tab changes often */
			update_tab_title_width(v, count);
		}
		if (!v->tt_width)
			update_tab_title_width(v, count);
		if (v->tt_width > trunc_min_w) {
			max_trunc_w += v->tt_width - trunc_min_w;
			trunc_count++;
		}
		total_len += v->tt_width;
		count++;
	}

	if (total_len <= window->w) {
		left_idx = 0;
	} else {
		int extra = total_len - window->w;

		if (extra <= max_trunc_w) {
			/* All tabs fit to screen after truncating some titles */
			int avg = extra / trunc_count;
			int mod = extra % trunc_count;

			idx = 0;
			list_for_each_entry(v, &window->views, node) {
				int w = v->tt_width - trunc_min_w;
				if (w > 0) {
					w = avg;
					if (mod) {
						w++;
						mod--;
					}
				}
				if (w > 0)
					v->tt_truncated_width = v->tt_width - w;
				idx++;
			}
			left_idx = 0;
		} else {
			/* Need to truncate all long titles but there's still
			 * not enough space for all tabs */
			int min_left_idx, max_left_idx, w;

			idx = 0;
			list_for_each_entry(v, &window->views, node) {
				w = v->tt_width - trunc_min_w;
				if (w > 0)
					v->tt_truncated_width = v->tt_width - w;
				idx++;
			}

			w = 0;
			max_left_idx = count;
			list_for_each_entry_reverse(v, &window->views, node) {
				w += v->tt_truncated_width;
				if (w > window->w)
					break;
				max_left_idx--;
			}

			w = 0;
			min_left_idx = count;
			list_for_each_entry_reverse(v, &window->views, node) {
				if (w || v == view)
					w += v->tt_truncated_width;
				if (w > window->w)
					break;
				min_left_idx--;
			}
			if (left_idx < min_left_idx)
				left_idx = min_left_idx;
			if (left_idx > max_left_idx)
				left_idx = max_left_idx;
		}
	}

	buf_reset(window->x, window->w, 0);
	buf_move_cursor(window->x, window->y - 1);
	buf_set_color(&tab_inactive_color->color);

	idx = -1;
	list_for_each_entry(v, &window->views, node) {
		if (++idx < left_idx)
			continue;

		if (obuf.x + v->tt_truncated_width > window->w)
			break;

		if (v == view)
			buf_set_color(&tab_active_color->color);
		print_tab_title(v, idx, v->tt_width - v->tt_truncated_width);
		if (v == view)
			buf_set_color(&tab_inactive_color->color);
	}
	buf_set_color(&tab_bar_color->color);
	buf_clear_eol();
}

void update_status_line(const char *misc_status)
{
	char lbuf[256];
	char rbuf[256];
	int lw, rw;

	buf_reset(window->x, window->w, 0);
	buf_move_cursor(window->x, window->y + window->h);
	buf_set_color(&statusline_color->color);
	lw = format_status(lbuf, sizeof(lbuf), options.statusline_left, misc_status);
	rw = format_status(rbuf, sizeof(rbuf), options.statusline_right, misc_status);
	if (term_flags & TERM_UTF8) {
		lw = u_str_width(lbuf, lw);
		rw = u_str_width(rbuf, rw);
	}
	if (lw + rw <= window->w) {
		buf_add_str(lbuf);
		buf_set_bytes(' ', window->w - lw - rw);
		buf_add_str(rbuf);
	} else {
		buf_add_str(lbuf);
		obuf.x = window->w - rw;
		buf_move_cursor(window->x + window->w - rw, window->y + window->h);
		buf_add_str(rbuf);
	}
}

static int get_char_width(unsigned int *idx)
{
	if (term_flags & TERM_UTF8) {
		return u_char_width(u_buf_get_char(cmdline.buffer, cmdline.len, idx));
	} else {
		int i = *idx;
		char ch = cmdline.buffer[i++];

		*idx = i;
		if (ch >= 0x20 && ch != 0x7f)
			return 1;
		return 2;
	}
}

int print_command(char prefix)
{
	unsigned int i, w;
	uchar u;
	int x;

	// width of characters up to and including cursor position
	w = 1; // ":" (prefix)
	i = 0;
	while (i <= cmdline_pos && cmdline.buffer[i])
		w += get_char_width(&i);
	if (!cmdline.buffer[cmdline_pos])
		w++;

	if (w > window->w)
		obuf.scroll_x = w - window->w;

	buf_set_color(&commandline_color->color);
	i = 0;
	if (obuf.x < obuf.scroll_x) {
		buf_skip(prefix, 0);
		while (obuf.x < obuf.scroll_x && cmdline.buffer[i]) {
			u = term_get_char(cmdline.buffer, cmdline.len, &i);
			buf_skip(u, term_flags & TERM_UTF8);
		}
	} else {
		buf_put_char(prefix, 0);
	}

	x = obuf.x - obuf.scroll_x;
	while (cmdline.buffer[i]) {
		BUG_ON(obuf.x > obuf.scroll_x + obuf.width);
		u = term_get_char(cmdline.buffer, cmdline.len, &i);
		if (!buf_put_char(u, term_flags & TERM_UTF8))
			break;
		if (i <= cmdline_pos)
			x = obuf.x - obuf.scroll_x;
	}
	return x;
}

void print_message(const char *msg, int is_error)
{
	const struct term_color *color = &commandline_color->color;
	unsigned int i = 0;

	if (msg[0])
		color = is_error ? &errormsg_color->color : &infomsg_color->color;
	buf_set_color(color);
	while (msg[i]) {
		uchar u = term_get_char(msg, i + 4, &i);
		if (!buf_put_char(u, term_flags & TERM_UTF8))
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
		color = default_color->color;

	if (nontext)
		mask_color(&color, &nontext_color->color);
	if (wserror)
		mask_color(&color, &wserror_color->color);
	if (selecting() && cur_offset >= sel_so && cur_offset < sel_eo)
		mask_color(&color, &selection_color->color);
	else if (current_line == view->cy)
		mask_color(&color, &currentline_color->color);
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

static int is_non_text(uchar u)
{
	if (u < 0x20)
		return u != '\t' || options.display_special;
	return u == 0x7f || !u_is_unicode(u);
}

static int whitespace_error(struct line_info *info, uchar u, unsigned int i)
{
	const char *line = info->line;
	int flags = buffer->options.ws_error;

	if (i < info->indent_size) {
		if (u == '\t' && flags & WSE_TAB_INDENT)
			return 1;
		if (u == ' ') {
			int count = 0, pos = i;

			while (pos > 0 && line[pos - 1] == ' ')
				pos--;
			while (pos < info->size && line[pos] == ' ') {
				pos++;
				count++;
			}

			if (count < buffer->options.tab_width && pos < info->size && line[pos] != '\t') {
				if (flags & WSE_SPACE_ALIGN)
					return 1;
			} else {
				if (flags & WSE_SPACE_INDENT)
					return 1;
			}
		}
	} else if (u == '\t' && flags & WSE_TAB_AFTER_INDENT) {
		return 1;
	}

	if (i >= info->trailing_ws_offset && flags & WSE_TRAILING) {
		/* don't highlight trailing ws if cursor is at or after the ws */
		if (current_line != view->cy || view->cx < info->trailing_ws_offset)
			return 1;
	}
	return 0;
}

static uchar screen_next_char(struct line_info *info)
{
	unsigned int count, pos = info->pos;
	uchar u = (unsigned char)info->line[pos];
	int ws_error = 0;

	if (likely(u < 0x80) || !buffer->utf8) {
		info->pos++;
		count = 1;
		if (u == '\t' || u == ' ')
			ws_error = whitespace_error(info, u, pos);
	} else {
		u = u_buf_get_char(info->line, info->size, &info->pos);
		count = info->pos - pos;
	}

	update_color(info->colors ? info->colors[pos] : NULL, is_non_text(u), ws_error);
	cur_offset += count;
	return u;
}

static void screen_skip_char(struct line_info *info, int utf8)
{
	unsigned int count, pos = info->pos;
	uchar u = (unsigned char)info->line[pos];

	if (likely(u < 0x80) || !buffer->utf8) {
		info->pos++;
		count = 1;

		if (u >= 0x20) {
			obuf.x++;
		} else if (u == '\t' && obuf.tab != TAB_CONTROL) {
			obuf.x += (obuf.x + obuf.tab_width) / obuf.tab_width * obuf.tab_width - obuf.x;
		} else {
			// control
			obuf.x += 2;
		}
	} else {
		u = u_buf_get_char(info->line, info->size, &info->pos);
		count = info->pos - pos;

		obuf.x += u_char_width(u);
	}
	cur_offset += count;
}

static void init_line_info(struct line_info *info, struct lineref *lr, struct hl_color **colors)
{
	int i;

	info->line = lr->line;
	info->size = lr->size;
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
	int utf8 = term_flags & TERM_UTF8;
	uchar u;

	/*
	 * Skip most characters using screen_skip_char() which is much
	 * faster than screen_next_char() which does color updating etc.
	 */
	while (obuf.x + 8 < obuf.scroll_x && info->pos < info->size)
		screen_skip_char(info, utf8);

	/*
	 * Skip rest. If a skipped character is wide (tab, control code
	 * etc.) and we need to display part of it then we must update
	 * color before calling buf_skip().
	 */
	while (obuf.x < obuf.scroll_x && info->pos < info->size) {
		u = screen_next_char(info);
		buf_skip(u, utf8);
	}

	/*
	 * Fully visible characters (except possibly the last one).
	 */
	while (info->pos < info->size) {
		BUG_ON(obuf.x > obuf.scroll_x + obuf.width);
		u = screen_next_char(info);
		if (!buf_put_char(u, utf8)) {
			// +1 for newline
			cur_offset += info->size - info->pos + 1;
			return;
		}
	}
	update_color(info->colors ? info->colors[info->pos] : NULL, 1, 0);
	cur_offset += 1; // newline
	if (options.display_special && obuf.x >= obuf.scroll_x)
		buf_put_char('$', utf8);
	buf_clear_eol();
}

void update_range(int y1, int y2)
{
	struct block_iter bi = view->cursor;
	int i, got_line;

	buf_reset(window->x, window->w, view->vx);
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
		buf_move_cursor(window->x, window->y + i);

		fill_line_nl_ref(&bi, &lr);
		colors = hl_line(lr.line, lr.size, current_line, &next_changed);
		if (lr.size && lr.line[lr.size - 1] == '\n') {
			// highlighter needs \n but other code does not want it
			lr.size--;
		}
		init_line_info(&info, &lr, colors);
		print_line(&info);

		got_line = block_iter_next_line(&bi);
		current_line++;

		if (next_changed && i + 1 == y2 && y2 < window->h) {
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
		buf_move_cursor(window->x, window->y + i++);
		buf_clear_eol();
	}

	if (i < y2)
		buf_set_color(&nontext_color->color);
	for (; i < y2; i++) {
		obuf.x = 0;
		buf_move_cursor(window->x, window->y + i);
		buf_ch('~');
		buf_clear_eol();
	}
}

void update_window_sizes(void)
{
	window->x = 0;
	window->y = options.show_tab_bar;
	window->w = screen_w;
	window->h = screen_h - window->y - 2;
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

void set_basic_colors(void)
{
	struct term_color c;

	c.fg = -1;
	c.bg = -1;
	c.attr = 0;
	default_color = set_highlight_color("default", &c);
	commandline_color = set_highlight_color("commandline", &c);
	nontext_color = set_highlight_color("nontext", &c);

	c.fg = -2;
	c.bg = -2;
	c.attr = ATTR_KEEP;
	currentline_color = set_highlight_color("currentline", &c);

	c.fg = 1;
	c.bg = -1;
	c.attr = ATTR_BOLD;
	errormsg_color = set_highlight_color("errormsg", &c);

	c.fg = 4;
	c.bg = -1;
	c.attr = ATTR_BOLD;
	infomsg_color = set_highlight_color("infomsg", &c);

	c.fg = -2;
	c.bg = 7;
	c.attr = ATTR_KEEP;
	selection_color = set_highlight_color("selection", &c);

	c.fg = -1;
	c.bg = 3;
	c.attr = 0;
	wserror_color = set_highlight_color("wserror", &c);

	c.fg = -1;
	c.bg = -1;
	c.attr = ATTR_BOLD;
	tab_active_color = set_highlight_color("activetab", &c);

	c.fg = 0;
	c.bg = 7;
	c.attr = 0;
	tab_inactive_color = set_highlight_color("inactivetab", &c);
	tab_bar_color = set_highlight_color("tabbar", &c);
	statusline_color = set_highlight_color("statusline", &c);
}
