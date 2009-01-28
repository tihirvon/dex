#include "window.h"
#include "editor.h"
#include "term.h"
#include "obuf.h"
#include "cmdline.h"
#include "commands.h"
#include "search.h"
#include "history.h"
#include "file-history.h"
#include "util.h"
#include "color.h"
#include "highlight.h"

#include <locale.h>
#include <langinfo.h>
#include <signal.h>

enum input_mode input_mode;
enum input_special input_special;
enum editor_status editor_status;
char misc_status[32];

static int separator;
static int nr_errors;
static int msg_is_error;
static char error_buf[256];

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

static int screen_w = 80;
static int screen_h = 24;
static int resized;
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
	snprintf(buf, sizeof(buf), " %d %s%s ",
		idx + 1,
		filename,
		buffer_modified(v->buffer) ? " *" : "");
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

static void print_tab_bar(void)
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
	int pos = *posp;
	int len = strlen(str);
	int n;

	if (separator && len) {
		if (pos + 2 < size)
			buf[pos++] = ' ';
		separator = 0;
	}

	n = size - pos - len - 1;
	if (n < 0)
		len += n;
	if (len > 0) {
		memcpy(buf + pos, str, len);
		pos += len;
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

static void print_status_line(void)
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
		buf_add_bytes(lbuf, strlen(lbuf));
		buf_set_bytes(' ', window->w - lw - rw);
		buf_add_bytes(rbuf, strlen(rbuf));
	} else {
		buf_add_bytes(lbuf, strlen(lbuf));
		buf_move_cursor(window->x + window->w - rw, window->y + window->h);
		buf_add_bytes(rbuf, strlen(rbuf));
	}

	misc_status[0] = 0;
}

static int get_char_width(int *idx)
{
	if (term_flags & TERM_UTF8) {
		return u_char_width(u_get_char(cmdline.buffer, idx));
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
	int i, w;
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
				u = u_get_char(cmdline.buffer, &i);
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
			u = u_get_char(cmdline.buffer, &i);
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

static void print_command_line(void)
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
			int i = 0;
			if (msg_is_error) {
				buf_set_color(&errormsg_color->color);
			} else {
				buf_set_color(&infomsg_color->color);
			}
			while (error_buf[i]) {
				uchar u;
				if (term_flags & TERM_UTF8) {
					u = u_get_char(error_buf, &i);
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
	else if (current_line == view->cy && currentline_color)
		mask_color(&color, &currentline_color->color);
	buf_set_color(&color);
}

static void selection_init(struct block_iter *cur)
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
		return (u != '\t' && u != '\n') || options.display_special;
	return u == 0x7f || !u_is_unicode(u);
}

static int indent_size;
static int trailing_ws_offset;

static int whitespace_error(uchar u, int i)
{
	int flags = buffer->options.ws_error;

	if (i < indent_size) {
		if (u == '\t' && flags & WSE_TAB_INDENT)
			return 1;
		if (u == ' ') {
			int count = 0, pos = i;

			while (pos > 0 && hl_buffer[pos - 1] == ' ')
				pos--;
			while (hl_buffer[pos] == ' ') {
				pos++;
				count++;
			}

			if (count < buffer->options.tab_width && hl_buffer[pos] != '\t') {
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

	if (i >= trailing_ws_offset && flags & WSE_TRAILING) {
		/* don't highlight trailing ws if cursor is at or after the ws */
		if (current_line != view->cy || view->cx < trailing_ws_offset)
			return 1;
	}
	return 0;
}

static unsigned int screen_next_char(int *idx, uchar *up)
{
	int count, ws_error = 0, i = *idx;
	uchar u;

	if (i == hl_buffer_len)
		return 0;

	u = u_get_char(hl_buffer, idx);
	count = *idx - i;
	if (current_hl_list)
		advance_hl(count);

	if (u == '\t' || u == ' ')
		ws_error = whitespace_error(u, i);

	update_color(nontext_color && is_non_text(u), ws_error);
	cur_offset += count;
	*up = u;
	return count;
}

static void print_line(struct block_iter *bi)
{
	int utf8 = term_flags & TERM_UTF8;
	int i;
	uchar u;

	fetch_line(bi);

	indent_size = 0;
	for (i = 0; hl_buffer[i] == '\t' || hl_buffer[i] == ' '; i++)
		indent_size++;

	trailing_ws_offset = INT_MAX;
	for (i = hl_buffer_len - 1; i >= 0; i--) {
		char ch = hl_buffer[i];
		if (ch != '\n' && ch != '\t' && ch != ' ')
			break;
		trailing_ws_offset = i;
	}

	i = 0;
	while (obuf.x < obuf.scroll_x) {
		if (!screen_next_char(&i, &u) || u == '\n') {
			buf_clear_eol();
			return;
		}
		buf_skip(u, utf8);
	}
	while (1) {
		BUG_ON(obuf.x > obuf.scroll_x + obuf.width);
		if (!screen_next_char(&i, &u))
			break;
		if (u == '\n') {
			if (options.display_special)
				buf_put_char('$', utf8);
			break;
		}

		if (!buf_put_char(u, utf8)) {
			int count = hl_buffer_len - i;
			if (count)
				advance_hl(count);
			cur_offset += count;
			return;
		}
	}
	buf_clear_eol();
}

static void update_range(int y1, int y2)
{
	struct block_iter bi = view->cursor;
	int i;

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

	selection_init(&bi);
	for (i = y1; i < y2; i++) {
		if (block_iter_eof(&bi))
			break;
		buf_move_cursor(window->x, window->y + i);
		print_line(&bi);
		current_line++;
	}

	if (i < y2 && current_line == view->cy) {
		// dummy empty line
		update_color(0, 0);
		buf_move_cursor(window->x, window->y + i++);
		buf_clear_eol();
	}

	if (i < y2 && nontext_color)
		buf_set_color(&nontext_color->color);
	for (; i < y2; i++) {
		buf_move_cursor(window->x, window->y + i);
		buf_ch('~');
		buf_clear_eol();
	}

	obuf.scroll_x = 0;
	print_status_line();
	print_command_line();
}

static void update_full(void)
{
	update_range(view->vy, view->vy + window->h);
}

static void update_status_line(void)
{
	print_status_line();
	print_command_line();
}

static void restore_cursor(void)
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

static void update_window_sizes(void)
{
	window->y = options.show_tab_bar;
	window->w = screen_w;
	window->h = screen_h - window->y - 2;
	obuf.width = screen_w;
}

static void update_screen_size(void)
{
	if (!term_get_size(&screen_w, &screen_h)) {
		if (screen_w < 3)
			screen_w = 3;
		if (screen_h < 3)
			screen_h = 3;
		update_window_sizes();
	}
}

static void update_everything(void)
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
}

static void debug_blocks(void)
{
#if DEBUG > 0
	struct block *blk;

	BUG_ON(list_empty(&buffer->blocks));

	list_for_each_entry(blk, &buffer->blocks, node) {
		BUG_ON(!blk->size && buffer->blocks.next->next != &buffer->blocks);
		BUG_ON(blk->size > blk->alloc);
		BUG_ON(count_nl(blk->data, blk->size) != blk->nl);
		BUG_ON(blk == view->cursor.blk && view->cursor.offset > blk->size);
	}
#endif
}

static void discard_paste(void)
{
	unsigned int size;
	char *text = term_read_paste(&size);
	free(text);
}

static void insert_paste(void)
{
	unsigned int size;
	char *text = term_read_paste(&size);
	insert(text, size);
	move_offset(buffer_offset() + size);
	free(text);
}

static void cmdline_insert_paste(void)
{
	unsigned int size;
	char *text = term_read_paste(&size);
	cmdline_insert_bytes(text, size);
	free(text);
}

static void any_key(void)
{
	unsigned int key;
	enum term_key_type type;

	printf("Press any key to continue\n");
	while (!term_read_key(&key, &type))
		;
	if (type == KEY_PASTE)
		discard_paste();
}

static void update_terminal_settings(void)
{
	// turn keypad on (makes cursor keys work)
	if (term_cap.ks)
		buf_escape(term_cap.ks);

	// use alternate buffer if possible
	if (term_cap.ti)
		buf_escape(term_cap.ti);
}

void ui_start(int prompt)
{
	term_raw();
	update_terminal_settings();
	if (prompt)
		any_key();
	update_everything();
}

void ui_end(void)
{
	struct term_color color = { -1, -1, 0 };

	buf_set_color(&color);
	buf_move_cursor(0, screen_h - 1);
	buf_show_cursor();

	// back to main buffer
	if (term_cap.te)
		buf_escape(term_cap.te);

	// turn keypad off
	if (term_cap.ke)
		buf_escape(term_cap.ke);

	buf_flush();
	term_cooked();
}

void error_msg(const char *format, ...)
{
	va_list ap;
	int pos = 0;

	if (config_file)
		pos = snprintf(error_buf, sizeof(error_buf), "%s:%d: ", config_file, config_line);

	va_start(ap, format);
	vsnprintf(error_buf + pos, sizeof(error_buf) - pos, format, ap);
	va_end(ap);
	msg_is_error = 1;
	update_flags |= UPDATE_STATUS_LINE;

	if (editor_status == EDITOR_INITIALIZING) {
		if (current_command)
			printf("%s: %s\n", current_command->name, error_buf);
		else
			printf("%s\n", error_buf);
	}
	nr_errors++;
}

void info_msg(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vsnprintf(error_buf, sizeof(error_buf), format, ap);
	va_end(ap);
	msg_is_error = 0;
	update_flags |= UPDATE_STATUS_LINE;
}

char get_confirmation(const char *choices, const char *format, ...)
{
	unsigned int key;
	int pos, i, count = strlen(choices);
	char def = 0;
	va_list ap;

	va_start(ap, format);
	vsnprintf(error_buf, sizeof(error_buf), format, ap);
	va_end(ap);

	pos = strlen(error_buf);
	error_buf[pos++] = ' ';
	error_buf[pos++] = '[';
	for (i = 0; i < count; i++) {
		if (isupper(choices[i]))
			def = tolower(choices[i]);
		error_buf[pos++] = choices[i];
		error_buf[pos++] = '/';
	}
	pos--;
	error_buf[pos++] = ']';
	error_buf[pos] = 0;

	update_cursor();
	buf_hide_cursor();
	update_full();
	restore_cursor();
	buf_show_cursor();
	buf_flush();

	while (1) {
		enum term_key_type type;

		if (term_read_key(&key, &type)) {
			if (type == KEY_PASTE)
				discard_paste();
			if (type != KEY_NORMAL)
				continue;

			if (key == 0x03) { // ^C
				key = 0;
				break;
			}
			if (key == '\r' && def) {
				key = def;
				break;
			}
			key = tolower(key);
			if (strchr(choices, key))
				break;
			if (key == def)
				break;
		} else if (resized) {
			resized = 0;
			update_terminal_settings();
			update_everything();
		}
	}
	error_buf[0] = 0;
	return key;
}

static int common_key(struct history *history, enum term_key_type type, unsigned int key)
{
	const char *str;

	switch (type) {
	case KEY_NORMAL:
		switch (key) {
		case 0x1b: // ESC
		case 0x03: // ^C
			cmdline_clear();
			input_mode = INPUT_NORMAL;
			// clear possible parse error
			error_buf[0] = 0;
			break;
		case 0x04: // ^D
			cmdline_delete();
			break;
		case 0x15: // ^U
			cmdline_delete_bol();
			break;
		case 0x16: // ^V
			input_special = INPUT_SPECIAL_UNKNOWN;
			break;

		case 0x01: // ^A
			cmdline_pos = 0;
			return 1;
		case 0x02: // ^B
			cmdline_prev_char();
			return 1;
		case 0x05: // ^E
			cmdline_pos = strlen(cmdline.buffer);
			return 1;
		case 0x06: // ^F
			cmdline_next_char();
			return 1;
		default:
			return 0;
		}
		break;
	case KEY_META:
		return 0;
	case KEY_SPECIAL:
		switch (key) {
		case SKEY_DELETE:
			cmdline_delete();
			break;
		case SKEY_BACKSPACE:
			cmdline_backspace();
			break;

		case SKEY_LEFT:
			cmdline_prev_char();
			return 1;
		case SKEY_RIGHT:
			cmdline_next_char();
			return 1;
		case SKEY_HOME:
			cmdline_pos = 0;
			return 1;
		case SKEY_END:
			cmdline_pos = strlen(cmdline.buffer);
			return 1;
		case SKEY_UP:
			str = history_search_forward(history, cmdline.buffer);
			if (str)
				cmdline_set_text(str);
			return 1;
		case SKEY_DOWN:
			str = history_search_backward(history);
			if (str)
				cmdline_set_text(str);
			return 1;
		default:
			return 0;
		}
		break;
	case KEY_PASTE:
		cmdline_insert_paste();
		break;
	}
	history_reset_search();
	return 1;
}

static void command_mode_key(enum term_key_type type, unsigned int key)
{
	switch (type) {
	case KEY_NORMAL:
		switch (key) {
		case '\r':
			reset_completion();
			input_mode = INPUT_NORMAL;
			handle_command(cmdline.buffer);
			history_add(&command_history, cmdline.buffer);
			cmdline_clear();
			break;
		case '\t':
			complete_command();
			break;
		default:
			reset_completion();
			cmdline_insert(key);
			break;
		}
		break;
	case KEY_META:
		return;
	case KEY_SPECIAL:
		return;
	case KEY_PASTE:
		return;
	}
	history_reset_search();
}

static void search_mode_key(enum term_key_type type, unsigned int key)
{
	switch (type) {
	case KEY_NORMAL:
		switch (key) {
		case '\r':
			if (cmdline.buffer[0]) {
				search(cmdline.buffer);
				history_add(&search_history, cmdline.buffer);
			} else {
				search_next();
			}
			cmdline_clear();
			input_mode = INPUT_NORMAL;
			break;
		default:
			cmdline_insert(key);
			break;
		}
		history_reset_search();
		break;
	case KEY_META:
		switch (key) {
		case 'c':
			options.ignore_case ^= 1;
			break;
		case 'r':
			search_init(current_search_direction() ^ 1);
			break;
		}
		break;
	case KEY_SPECIAL:
		break;
	case KEY_PASTE:
		break;
	}
}

static void handle_key(enum term_key_type type, unsigned int key)
{
	struct change_head *save_change_head = buffer->save_change_head;
	struct view *v = view;
	int update_tab_bar = 0;
	int show_tab_bar = options.show_tab_bar;
	int is_modified = buffer_modified(buffer);
	int cx = view->cx_display;
	int cy = view->cy;
	int vx = view->vx;
	int vy = view->vy;

	if (nr_pressed_keys) {
		handle_binding(type, key);
	} else {
		switch (input_mode) {
		case INPUT_NORMAL:
			switch (type) {
			case KEY_NORMAL:
				if (key == '\t') {
					insert_ch('\t');
				} else if (key == '\r') {
					insert_ch('\n');
				} else if (key == 0x1a) {
					ui_end();
					kill(0, SIGSTOP);
				} else if (key < 0x20) {
					handle_binding(type, key);
				} else {
					insert_ch(key);
				}
				break;
			case KEY_META:
				handle_binding(type, key);
				break;
			case KEY_SPECIAL:
				if (key == SKEY_DELETE) {
					delete_ch();
				} else if (key == SKEY_BACKSPACE) {
					erase();
				} else {
					handle_binding(type, key);
				}
				break;
			case KEY_PASTE:
				insert_paste();
				break;
			}
			break;
		case INPUT_COMMAND:
			if (common_key(&command_history, type, key)) {
				reset_completion();
			} else {
				command_mode_key(type, key);
			}
			update_flags |= UPDATE_STATUS_LINE;
			break;
		case INPUT_SEARCH:
			if (!common_key(&search_history, type, key))
				search_mode_key(type, key);
			update_flags |= UPDATE_STATUS_LINE;
			break;
		}
	}

	debug_blocks();
	update_cursor();

	if (vx != view->vx || vy != view->vy) {
		update_flags |= UPDATE_FULL;
	} else if (cx != view->cx_display || cy != view->cy ||
			save_change_head != buffer->save_change_head) {
		update_flags |= UPDATE_STATUS_LINE;

		if (cy != view->cy)
			update_flags |= UPDATE_RANGE;
		if (cx != view->cx_display)
			update_flags |= UPDATE_CURSOR_LINE;
	}
	if (v != view) {
		update_flags |= UPDATE_FULL;
		update_tab_bar = 1;
	} else if (is_modified != buffer_modified(buffer)) {
		update_tab_bar = 1;
	}
	if (show_tab_bar != options.show_tab_bar) {
		update_window_sizes();
		update_flags |= UPDATE_FULL;
		update_tab_bar = 1;
	}

	if (!update_flags)
		return;

	buf_hide_cursor();
	if (options.show_tab_bar && update_tab_bar)
		print_tab_bar();
	if (update_flags & UPDATE_FULL) {
		update_full();
	} else if (update_flags & UPDATE_RANGE) {
		int y1 = cy;
		int y2 = view->cy;
		if (y1 > y2) {
			int tmp = y1;
			y1 = y2;
			y2 = tmp;
		}
		update_range(y1, y2 + 1);
	} else if (update_flags & UPDATE_CURSOR_LINE) {
		update_range(view->cy, view->cy + 1);
	} else if (update_flags & UPDATE_STATUS_LINE) {
		update_status_line();
	}
	restore_cursor();
	buf_show_cursor();

	update_flags = 0;

	buf_flush();
}

static void insert_special(const char *buf, int size)
{
	buf_hide_cursor();
	switch (input_mode) {
	case INPUT_NORMAL:
		insert(buf, size);
		update_full();
		break;
	case INPUT_COMMAND:
	case INPUT_SEARCH:
		cmdline_insert_bytes(buf, size);
		update_status_line();
		break;
	}
	update_cursor();
	restore_cursor();
	buf_show_cursor();
	buf_flush();
}

static struct {
	int base;
	int max_chars;
	int value;
	int nr;
} raw_input;

static void raw_status(void)
{
	int i, value = raw_input.value;
	const char *str = "";
	char buf[7];

	if (input_special == INPUT_SPECIAL_UNKNOWN) {
		info_msg("Insert special character");
		goto update;
	}

	for (i = 0; i < raw_input.nr; i++) {
		buf[raw_input.nr - i - 1] = hex_tab[value % raw_input.base];
		value /= raw_input.base;
	}
	while (i < raw_input.max_chars)
		buf[i++] = ' ';
	buf[i] = 0;

	switch (input_special) {
	case INPUT_SPECIAL_NONE:
		break;
	case INPUT_SPECIAL_UNKNOWN:
		break;
	case INPUT_SPECIAL_OCT:
		str = "oct";
		break;
	case INPUT_SPECIAL_DEC:
		str = "dec";
		break;
	case INPUT_SPECIAL_HEX:
		str = "hex";
		break;
	case INPUT_SPECIAL_UNICODE:
		str = "unicode, hex";
		break;
	}

	info_msg("Insert %s <%s>", str, buf);
update:
	buf_hide_cursor();
	update_status_line();
	restore_cursor();
	buf_show_cursor();
	update_flags = 0;
	buf_flush();
}

static void handle_raw(enum term_key_type type, unsigned int key)
{
	char buf[4];

	if (type != KEY_NORMAL) {
		if (type == KEY_PASTE)
			discard_paste();
		input_special = INPUT_SPECIAL_NONE;
		return;
	}
	if (input_special == INPUT_SPECIAL_UNKNOWN) {
		raw_input.value = 0;
		raw_input.nr = 0;
		if (isdigit(key)) {
			input_special = INPUT_SPECIAL_DEC;
			raw_input.base = 10;
			raw_input.max_chars = 3;
		} else {
			switch (tolower(key)) {
			case 'o':
				input_special = INPUT_SPECIAL_OCT;
				raw_input.base = 8;
				raw_input.max_chars = 3;
				break;
			case 'x':
				input_special = INPUT_SPECIAL_HEX;
				raw_input.base = 16;
				raw_input.max_chars = 2;
				break;
			case 'u':
				input_special = INPUT_SPECIAL_UNICODE;
				raw_input.base = 16;
				raw_input.max_chars = 6;
				break;
			default:
				buf[0] = key;
				insert_special(buf, 1);
				input_special = INPUT_SPECIAL_NONE;
			}
			return;
		}
	}

	if (key != '\r') {
		unsigned int n;

		if (isdigit(key)) {
			n = key - '0';
		} else if (key >= 'a' && key <= 'f') {
			n = key - 'a' + 10;
		} else if (key >= 'A' && key <= 'F') {
			n = key - 'A' + 10;
		} else {
			input_special = INPUT_SPECIAL_NONE;
			return;
		}
		if ((raw_input.base == 8 && n > 7) || (raw_input.base == 10 && n > 9)) {
			input_special = INPUT_SPECIAL_NONE;
			return;
		}
		raw_input.value *= raw_input.base;
		raw_input.value += n;
		if (++raw_input.nr < raw_input.max_chars)
			return;
	}

	if (input_special == INPUT_SPECIAL_UNICODE && u_is_unicode(raw_input.value)) {
		int idx = 0;
		u_set_char_raw(buf, &idx, raw_input.value);
		insert_special(buf, idx);
	}
	if (input_special != INPUT_SPECIAL_UNICODE && raw_input.value <= 255) {
		buf[0] = raw_input.value;
		insert_special(buf, 1);
	}
	input_special = INPUT_SPECIAL_NONE;
}

static void handle_input(enum term_key_type type, unsigned int key)
{
	if (input_special)
		handle_raw(type, key);
	else
		handle_key(type, key);
	if (input_special)
		raw_status();
}

static void set_signal_handler(int signum, void (*handler)(int))
{
	struct sigaction act;

	memset(&act, 0, sizeof(act));
	sigemptyset(&act.sa_mask);
	act.sa_handler = handler;
	sigaction(signum, &act, NULL);
}

static void handle_sigtstp(int signum)
{
	ui_end();
	kill(0, SIGSTOP);
}

static void handle_sigcont(int signum)
{
	ui_start(0);
}

static void handle_sigwinch(int signum)
{
	resized = 1;
}

static void set_basic_colors(void)
{
	struct term_color none, c;

	none.fg = -1;
	none.bg = -1;
	none.attr = 0;
	default_color = set_highlight_color("default", &none);

	c.fg = 0;
	c.bg = 7;
	c.attr = 0;
	selection_color = set_highlight_color("selection", &c);
	statusline_color = set_highlight_color("statusline", &c);
	commandline_color = set_highlight_color("commandline", &none);
	errormsg_color = set_highlight_color("errormsg", &none);
	infomsg_color = set_highlight_color("infomsg", &none);

	c.fg = -1;
	c.bg = 3;
	c.attr = 0;
	wserror_color = set_highlight_color("wserror", &c);

	c.fg = -1;
	c.bg = 7;
	c.attr = 0;
	tab_bar_color = set_highlight_color("tabbar", &c);

	c.fg = -1;
	c.bg = -1;
	c.attr = ATTR_BOLD;
	tab_active_color = set_highlight_color("activetab", &c);

	c.fg = 0;
	c.bg = 7;
	c.attr = 0;
	tab_inactive_color = set_highlight_color("inactivetab", &c);
}

static void close_all_views(void)
{
	struct window *w;

	list_for_each_entry(w, &windows, node) {
		struct list_head *item = w->views.next;
		while (item != &w->views) {
			struct list_head *next = item->next;
			view_delete(VIEW(item));
			item = next;
		}
	}
}

int main(int argc, char *argv[])
{
	int i;
	unsigned int flags = TERM_USE_TERMCAP | TERM_USE_TERMINFO;
	const char *tag = NULL;
	const char *rc = NULL;
	const char *command = NULL;
	const char *charset;
	struct view *tmp_view;

	init_misc();

	for (i = 1; i < argc; i++) {
		const char *opt = argv[i];

		if (strcmp(opt, "-C") == 0) {
			flags &= ~TERM_USE_TERMCAP;
			continue;
		}
		if (strcmp(opt, "-I") == 0) {
			flags &= ~TERM_USE_TERMINFO;
			continue;
		}
		if (strcmp(opt, "--help") == 0) {
			return 0;
		}
		if (!strcmp(opt, "-t") || !strcmp(opt, "--tag")) {
			if (++i == argc) {
				fprintf(stderr, "missing argument for option %s\n", opt);
				return 1;
			}
			tag = argv[i];
			continue;
		}
		if (!strcmp(opt, "-r") || !strcmp(opt, "--rc")) {
			if (++i == argc) {
				fprintf(stderr, "missing argument for option %s\n", opt);
				return 1;
			}
			rc = argv[i];
			continue;
		}
		if (!strcmp(opt, "-c") || !strcmp(opt, "--command")) {
			if (++i == argc) {
				fprintf(stderr, "missing argument for option %s\n", opt);
				return 1;
			}
			command = argv[i];
			continue;
		}
		if (strcmp(opt, "--") == 0) {
			i++;
			break;
		}
		if (*opt != '-')
			break;

		fprintf(stderr, "invalid option flag %s\n", opt);
		return 1;
	}

	if (!rc)
		rc = editor_file("rc");

	setlocale(LC_CTYPE, "");
	charset = nl_langinfo(CODESET);
	if (strcmp(charset, "UTF-8") == 0)
		flags |= TERM_UTF8;

	/* Fast regexec() etc. please.
	 * This doesn't change environment so subprocesses are not affected.
	 */
	setlocale(LC_CTYPE, "C");

	if (term_init(NULL, flags))
		return 1;

	init_options();
	set_basic_colors();

	window = window_new();
	update_screen_size();

	/* there must be always at least one buffer open */
	tmp_view = open_empty_buffer();
	tmp_view->rc_tmp = 1;
	set_view(tmp_view);
	read_config(rc, 0);
	if (command)
		handle_command(command);
	if (tag)
		goto_tag(tag);

	update_all_syntax_colors();
	sort_aliases();
	currentline_color = find_color("currentline");
	nontext_color = find_color("nontext");

	/* Terminal does not generate signals for control keys. */
	set_signal_handler(SIGINT, SIG_IGN);
	set_signal_handler(SIGQUIT, SIG_IGN);

	/* Terminal does not generate signal for ^Z but someone can send
	 * us SIGSTOP or SIGTSTP nevertheless.
	 */
	set_signal_handler(SIGTSTP, handle_sigtstp);

	set_signal_handler(SIGCONT, handle_sigcont);
	set_signal_handler(SIGWINCH, handle_sigwinch);

	obuf.alloc = 8192;
	obuf.buf = xmalloc(obuf.alloc);
	obuf.width = 80;

	load_file_history();
	history_load(&command_history, editor_file("command-history"));
	history_load(&search_history, editor_file("search-history"));

	/* Initialize terminal but don't update screen yet.  Also display
	 * "Press any key to continue" prompt if there were any errors
	 * during reading configuration files.
	 */
	term_raw();
	update_terminal_settings();
	if (nr_errors)
		any_key();
	error_buf[0] = 0;

	/* You can have "quit" in the rc file for testing purposes. */
	if (editor_status == EDITOR_INITIALIZING)
		editor_status = EDITOR_RUNNING;

	for (; i < argc; i++)
		open_buffer(argv[i], 0);

	/* remove the temporary view we created before reading rc
	 * if the view's buffer was not touched
	 */
	list_for_each_entry(tmp_view, &window->views, node) {
		if (tmp_view->rc_tmp) {
			if (!tmp_view->buffer->change_head.prev) {
				set_view(tmp_view);
				remove_view();
			}
			break;
		}
	}

	if (list_empty(&window->views))
		open_empty_buffer();
	set_view(VIEW(window->views.next));

	update_everything();

	while (editor_status == EDITOR_RUNNING) {
		if (resized) {
			resized = 0;
			update_terminal_settings();
			update_everything();
		} else {
			unsigned int key;
			enum term_key_type type;
			if (term_read_key(&key, &type)) {
				/* clear possible error message */
				error_buf[0] = 0;
				handle_input(type, key);
			}
		}
	}
	ui_end();
	mkdir(editor_file(""), 0755);
	history_save(&command_history, editor_file("command-history"));
	history_save(&search_history, editor_file("search-history"));
	close_all_views();
	save_file_history();
	return 0;
}
