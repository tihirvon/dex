#include "screen.h"
#include "window.h"
#include "obuf.h"
#include "cmdline.h"
#include "search.h"
#include "color.h"
#include "highlight.h"
#include "buffer-highlight.h"

struct line_info {
	// struct lineref
	const char *line;
	unsigned int size;

	unsigned int pos;
	unsigned int indent_size;
	unsigned int trailing_ws_offset;
};

int screen_w = 80;
int screen_h = 24;

int nr_errors;
int msg_is_error;
char error_buf[256];

static char misc_status[32];

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

static int separator;
static int cmdline_x;
static int current_line;

static void update_tab_title(struct view *v, int idx, int skip)
{
	char buf[512];
	const char *filename = v->buffer->filename;

	if (!filename)
		filename = "(No name)";
	if (skip > 0) {
		if (term_flags & TERM_UTF8)
			filename += u_skip_chars(filename, &skip);
		else
			filename += skip;
	}
	snprintf(buf, sizeof(buf), " %d%s%s ",
		idx + 1,
		buffer_modified(v->buffer) ? "+" : ":",
		filename);
	if (skip >= 0) {
		buf_add_bytes(buf, v->tt_truncated_width);
	} else {
		if (term_flags & TERM_UTF8)
			v->tt_width = u_str_width(buf);
		else
			v->tt_width = strlen(buf);
		v->tt_truncated_width = v->tt_width;
	}
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
			/* title of current tab changes ofter */
			update_tab_title(v, count, -1);
		}
		if (!v->tt_width)
			update_tab_title(v, count, -1);
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
		update_tab_title(v, idx, v->tt_width - v->tt_truncated_width);
		obuf.x += v->tt_truncated_width;
		if (v == view)
			buf_set_color(&tab_inactive_color->color);
	}
	buf_set_color(&tab_bar_color->color);
	buf_clear_eol();
}

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

static void format_status(char *buf, int size, const char *format)
{
	int pos = 0;
	int got_char;
	uchar u;

	separator = 0;
	got_char = buffer_get_char(&u);
	if (got_char)
		u &= ~U_INVALID_MASK;
	while (pos < size - 1 && *format) {
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
}

void update_status_line(void)
{
	char lbuf[256];
	char rbuf[256];
	int lw, rw;

	if (input_mode == INPUT_SEARCH) {
		snprintf(misc_status, sizeof(misc_status), "[%s]",
			options.ignore_case ? "case-insensitive" : "case-sensitive");
	} else if (view->sel.blk) {
		struct selection_info info;

		init_selection(&info);
		fill_selection_info(&info);
		if (view->sel_is_lines) {
			snprintf(misc_status, sizeof(misc_status), "[%d lines]", info.nr_lines);
		} else {
			snprintf(misc_status, sizeof(misc_status), "[%d chars]", info.nr_chars);
		}
	}

	buf_move_cursor(window->x, window->y + window->h);
	buf_set_color(&statusline_color->color);
	format_status(lbuf, sizeof(lbuf), options.statusline_left);
	format_status(rbuf, sizeof(rbuf), options.statusline_right);
	if (term_flags & TERM_UTF8) {
		lw = u_str_width(lbuf);
		rw = u_str_width(rbuf);
	} else {
		lw = strlen(lbuf);
		rw = strlen(rbuf);
	}
	if (lw + rw <= window->w) {
		buf_add_str(lbuf);
		buf_set_bytes(' ', window->w - lw - rw);
		buf_add_str(rbuf);
	} else {
		buf_add_str(lbuf);
		buf_move_cursor(window->x + window->w - rw, window->y + window->h);
		buf_add_str(rbuf);
	}

	misc_status[0] = 0;
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

static void print_command(uchar prefix)
{
	unsigned int i, w;
	uchar u;

	// width of characters up to and including cursor position
	w = 1; // ":" (prefix)
	i = 0;
	while (i <= cmdline_pos && cmdline.buffer[i])
		w += get_char_width(&i);
	if (!cmdline.buffer[cmdline_pos])
		w++;

	obuf.tab_width = 8;
	obuf.scroll_x = 0;
	if (w > window->w)
		obuf.scroll_x = w - window->w;

	i = 0;
	if (obuf.x < obuf.scroll_x) {
		buf_skip(prefix, 0);
		while (obuf.x < obuf.scroll_x && cmdline.buffer[i]) {
			if (term_flags & TERM_UTF8) {
				u = u_buf_get_char(cmdline.buffer, cmdline.len, &i);
			} else {
				u = cmdline.buffer[i++];
			}
			buf_skip(u, term_flags & TERM_UTF8);
		}
	} else {
		buf_put_char(prefix, 0);
	}

	cmdline_x = obuf.x - obuf.scroll_x;
	while (cmdline.buffer[i]) {
		BUG_ON(obuf.x > obuf.scroll_x + obuf.width);
		if (term_flags & TERM_UTF8) {
			u = u_buf_get_char(cmdline.buffer, cmdline.len, &i);
		} else {
			u = cmdline.buffer[i++];
		}
		if (!buf_put_char(u, term_flags & TERM_UTF8))
			break;
		if (i <= cmdline_pos)
			cmdline_x = obuf.x - obuf.scroll_x;
	}
	buf_clear_eol();
}

void update_command_line(void)
{
	buf_move_cursor(0, screen_h - 1);
	switch (input_mode) {
	case INPUT_COMMAND:
		buf_set_color(&commandline_color->color);
		print_command(':');
		break;
	case INPUT_SEARCH:
		buf_set_color(&commandline_color->color);
		print_command(current_search_direction() == SEARCH_FWD ? '/' : '?');
		break;
	default:
		obuf.tab_width = 8;
		obuf.scroll_x = 0;
		if (error_buf[0]) {
			unsigned int len = strlen(error_buf);
			unsigned int i = 0;

			if (msg_is_error) {
				buf_set_color(&errormsg_color->color);
			} else {
				buf_set_color(&infomsg_color->color);
			}
			while (i < len) {
				uchar u;
				if (term_flags & TERM_UTF8) {
					u = u_buf_get_char(error_buf, len, &i);
				} else {
					u = error_buf[i++];
				}
				if (!buf_put_char(u, term_flags & TERM_UTF8))
					break;
			}
		} else {
			buf_set_color(&commandline_color->color);
		}
		buf_clear_eol();
		break;
	}
}

// selection start / end buffer byte offsets
static unsigned int sel_so, sel_eo;
static unsigned int cur_offset;

static const struct hl_color *current_hl_color;
static const struct hl_list *current_hl_list;
static int current_hl_entry_idx;
static int current_hl_entry_pos;

static void advance_hl(unsigned int count)
{
	BUG_ON(!buffer->syn);
	while (1) {
		const struct hl_entry *e = &current_hl_list->entries[current_hl_entry_idx];
		unsigned int avail = hl_entry_len(e) - current_hl_entry_pos;

		BUG_ON(!current_hl_list->count);
		if (avail >= count) {
			union syntax_node *n = idx_to_syntax_node(hl_entry_idx(e));
			unsigned int type = hl_entry_type(e);
			current_hl_entry_pos += count;
			current_hl_color = NULL;
			if (type == HL_ENTRY_SOC)
				current_hl_color = n->context.scolor;
			if (type == HL_ENTRY_EOC)
				current_hl_color = n->context.ecolor;
			if (!current_hl_color)
				current_hl_color = n->any.color;
			return;
		}
		count -= avail;
		current_hl_entry_idx++;
		current_hl_entry_pos = 0;
		if (current_hl_entry_idx == current_hl_list->count) {
			BUG_ON(current_hl_list->node.next == &buffer->hl_head);
			current_hl_list = HL_LIST(current_hl_list->node.next);
			current_hl_entry_idx = 0;
		}
	}
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

static void update_color(int nontext, int wserror)
{
	struct term_color color;

	if (current_hl_color)
		color = current_hl_color->color;
	else
		color = default_color->color;
	if (nontext)
		mask_color(&color, &nontext_color->color);
	if (wserror)
		mask_color(&color, &wserror_color->color);
	if (view->sel.blk && cur_offset >= sel_so && cur_offset <= sel_eo)
		mask_color(&color, &selection_color->color);
	else if (current_line == view->cy)
		mask_color(&color, &currentline_color->color);
	buf_set_color(&color);
}

static void set_hl_pos(struct block_iter *cur)
{
	cur_offset = block_iter_get_offset(cur);

	current_hl_color = NULL;
	current_hl_list = NULL;
	current_hl_entry_idx = 0;
	current_hl_entry_pos = 0;
	if (!list_empty(&buffer->hl_head)) {
		current_hl_list = HL_LIST(buffer->hl_head.next);
		advance_hl(cur_offset);
	}
}

static void selection_init(void)
{
	if (view->sel.blk) {
		struct block_iter si, ei;

		si = view->sel;
		ei = view->cursor;

		sel_so = block_iter_get_offset(&si);
		sel_eo = block_iter_get_offset(&ei);
		if (sel_so > sel_eo) {
			unsigned int to = sel_eo;
			sel_eo = sel_so;
			sel_so = to;
			if (view->sel_is_lines) {
				sel_so -= block_iter_bol(&ei);
				sel_eo += count_bytes_eol(&si) - 1;
			}
		} else if (view->sel_is_lines) {
			sel_so -= block_iter_bol(&si);
			sel_eo += count_bytes_eol(&ei) - 1;
		}
	}
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
	int ws_error = 0;
	uchar u;

	if (buffer->utf8) {
		u = u_buf_get_char(info->line, info->size, &info->pos);
		count = info->pos - pos;
	} else {
		u = (unsigned char)info->line[pos];
		info->pos++;
		count = 1;
	}
	if (current_hl_list)
		advance_hl(count);

	if (u == '\t' || u == ' ')
		ws_error = whitespace_error(info, u, pos);

	update_color(is_non_text(u), ws_error);
	cur_offset += count;
	return u;
}

static void init_line_info(struct line_info *info, struct block_iter *bi)
{
	int i;

	fill_line_ref(bi, (struct lineref *)info);
	info->pos = 0;

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

	while (obuf.x < obuf.scroll_x && info->pos < info->size) {
		u = screen_next_char(info);
		buf_skip(u, utf8);
	}
	while (info->pos < info->size) {
		BUG_ON(obuf.x > obuf.scroll_x + obuf.width);
		u = screen_next_char(info);
		if (!buf_put_char(u, utf8)) {
			int count = info->size - info->pos;
			if (count && current_hl_list)
				advance_hl(count);
			cur_offset += count;
			return;
		}
	}
	update_color(1, 0);
	cur_offset += 1;
	if (options.display_special && obuf.x >= obuf.scroll_x)
		buf_put_char('$', utf8);
	buf_clear_eol();
}

void update_range(int y1, int y2)
{
	struct block_iter bi = view->cursor;
	int i, got_line;

	obuf.tab_width = buffer->options.tab_width;
	obuf.scroll_x = view->vx;

	for (i = 0; i < view->cy - y1; i++)
		block_iter_prev_line(&bi);
	for (i = 0; i < y1 - view->cy; i++)
		block_iter_next_line(&bi);
	block_iter_bol(&bi);

	current_line = y1;
	y1 -= view->vy;
	y2 -= view->vy;

	set_hl_pos(&bi);
	selection_init();

	got_line = !block_iter_eof(&bi);
	for (i = y1; got_line && i < y2; i++) {
		struct line_info info;

		buf_move_cursor(window->x, window->y + i);

		init_line_info(&info, &bi);
		print_line(&info);

		got_line = block_iter_next_line(&bi);
		if (got_line && current_hl_list)
			advance_hl(1);
		current_line++;
	}

	if (i < y2 && current_line == view->cy) {
		// dummy empty line
		update_color(0, 0);
		buf_move_cursor(window->x, window->y + i++);
		buf_clear_eol();
	}

	if (i < y2)
		buf_set_color(&nontext_color->color);
	for (; i < y2; i++) {
		buf_move_cursor(window->x, window->y + i);
		buf_ch('~');
		buf_clear_eol();
	}

	obuf.scroll_x = 0;
	update_status_line();
	update_command_line();
}

void update_full(void)
{
	update_range(view->vy, view->vy + window->h);
}

void restore_cursor(void)
{
	switch (input_mode) {
	case INPUT_NORMAL:
		buf_move_cursor(
			window->x + view->cx_display - view->vx,
			window->y + view->cy - view->vy);
		break;
	case INPUT_COMMAND:
	case INPUT_SEARCH:
		buf_move_cursor(cmdline_x, screen_h - 1);
		break;
	}
}

void update_window_sizes(void)
{
	window->y = options.show_tab_bar;
	window->w = screen_w;
	window->h = screen_h - window->y - 2;
	obuf.width = screen_w;
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

void update_everything(void)
{
	update_screen_size();
	update_cursor();
	buf_hide_cursor();
	if (options.show_tab_bar)
		print_tab_bar();
	update_full();
	restore_cursor();
	buf_show_cursor();
	buf_flush();

	update_flags = 0;
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
