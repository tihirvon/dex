#include "buffer.h"
#include "term.h"
#include "obuf.h"

#include <locale.h>
#include <langinfo.h>
#include <signal.h>

static enum {
	INPUT_NORMAL,
} input_mode;
int running = 1;

static int received_signal;

static int add_status_str(char *buf, int size, int *posp, const char *str)
{
	int w, len, pos = *posp;

	len = strlen(str);
	w = len;
	if (term_flags & TERM_UTF8)
		w = u_strlen(str);
	if (len < size) {
		memcpy(buf + pos, str, len);
		*posp = pos + len;
		return w;
	}
	return 0;
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

static int add_status_pos(char *buf, int size, int *posp)
{
	int h = window->h;
	int pos = view->vy;
	int d;

	if (buffer->nl <= h) {
		if (pos)
			return add_status_str(buf, size, posp, "Bot");
		return add_status_str(buf, size, posp, "All");
	}
	if (pos == 0)
		return add_status_str(buf, size, posp, "Top");
	if (pos + h - 1 >= buffer->nl)
		return add_status_str(buf, size, posp, "Bot");

	d = buffer->nl - (h - 1);
	return add_status_str(buf, size, posp, ssprintf("%2d%%", (pos * 100 + d / 2) / d));
}

static int format_status(char *buf, int size, const char *format)
{
	int pos = 0;
	int w = 0;
	int got_char;
	uchar u;

	got_char = buffer_get_char(&u);
	while (pos < size - 1 && *format) {
		char ch = *format++;
		if (ch != '%') {
			buf[pos++] = ch;
			w++;
		} else {
			ch = *format++;
			switch (ch) {
			case 'f':
				w += add_status_str(buf, size, &pos,
						buffer->filename ? buffer->filename : "(No name)");
				break;
			case 'm':
				if (buffer_modified(buffer))
					w += add_status_str(buf, size, &pos, "[+]");
				break;
			case 'y':
				w += add_status_str(buf, size, &pos, ssprintf("%d", view->cy));
				break;
			case 'x':
				w += add_status_str(buf, size, &pos, ssprintf("%d", view->cx));
				break;
			case 'X':
				w += add_status_str(buf, size, &pos, ssprintf("%d", view->cx_idx));
				if (view->cx != view->cx_idx)
					w += add_status_str(buf, size, &pos, ssprintf("-%d", view->cx));
				break;
			case 'c':
				if (got_char)
					w += add_status_str(buf, size, &pos, ssprintf("%3d", u));
				break;
			case 'C':
				if (got_char)
					w += add_status_str(buf, size, &pos, ssprintf("0x%02x", u));
				break;
			case 'p':
				w += add_status_pos(buf, size, &pos);
				break;
			case '%':
				buf[pos++] = '%';
				break;
			default:
				buf[pos++] = '%';
				if (pos < size - 1)
					buf[pos++] = ch;
				break;
			}
		}
	}
	buf[pos] = 0;
	return w;
}

static void print_status_line(void)
{
	const char *lformat = " %f %m";
	const char *rformat = " %y,%X   %c %C   %p ";
	char lbuf[256];
	char rbuf[256];
	int lw, rw;

	buf_move_cursor(0, window->h);
	buf_set_colors(0, 7);
	lw = format_status(lbuf, sizeof(lbuf), lformat);
	rw = format_status(rbuf, sizeof(rbuf), rformat);
	if (lw + rw <= window->w) {
		buf_add_bytes(lbuf, strlen(lbuf));
		buf_set_bytes(' ', window->w - lw - rw);
		buf_add_bytes(rbuf, strlen(rbuf));
	} else {
		buf_add_bytes(lbuf, strlen(lbuf));
		buf_move_cursor(window->w - rw, window->h);
		buf_add_bytes(rbuf, strlen(rbuf));
	}
}

static void print_command_line(void)
{
	buf_move_cursor(0, window->h + 1);
	buf_set_colors(-1, -1);
	buf_clear_eol();
}

// selection start / end buffer byte offsets
static unsigned int sel_so, sel_eo;
static unsigned int cur_offset;
static int sel_started;
static int sel_ended;

static void selection_check(void)
{
	if (view->sel_blk) {
		if (!sel_started && cur_offset >= sel_so) {
			buf_set_colors(0, 7);
			sel_started = 1;
		}
		if (!sel_ended && cur_offset > sel_eo) {
			buf_set_colors(-1, -1);
			sel_ended = 1;
		}
	}
}

static void selection_init(struct block_iter *cur)
{
	if (view->sel_blk) {
		struct block_iter si, ei;

		sel_started = 0;
		sel_ended = 0;
		cur_offset = buffer_get_offset(cur->blk, cur->offset);

		si.head = &buffer->blocks;
		si.blk = view->sel_blk;
		si.offset = view->sel_offset;

		ei.head = &buffer->blocks;
		ei.blk = view->cblk;
		ei.offset = view->coffset;

		sel_so = buffer_get_offset(si.blk, si.offset);
		sel_eo = buffer_get_offset(ei.blk, ei.offset);
		if (sel_so > sel_eo) {
			unsigned int to = sel_eo;
			sel_eo = sel_so;
			sel_so = to;
			if (view->sel_is_lines) {
				sel_so -= block_iter_bol(&ei);
				sel_eo += count_bytes_eol(&si);
			}
		} else if (view->sel_is_lines) {
			sel_so -= block_iter_bol(&si);
			sel_eo += count_bytes_eol(&ei);
		}
		selection_check();
	}
}

static unsigned int screen_next_char(struct block_iter *bi, uchar *u)
{
	unsigned int count = buffer->next_char(bi, u);

	selection_check();
	cur_offset += count;
	return count;
}

static unsigned int screen_next_line(struct block_iter *bi)
{
	unsigned int count = block_iter_next_line(bi);

	cur_offset += count;
	selection_check();
	return count;
}

static void print_line(struct block_iter *bi)
{
	int n, tw = buffer->tab_width;
	int utf8 = buffer->utf8;
	uchar u;

	while (obuf.x < obuf.scroll_x) {
		if (!screen_next_char(bi, &u) || u == '\n') {
			buf_clear_eol();
			return;
		}
		if (u < 0x80 || !utf8) {
			if (u >= 0x20) {
				obuf.x++;
			} else if (u == '\t') {
				obuf.x += (obuf.x + tw) / tw * tw - obuf.x;
			} else {
				// control
				obuf.x += 2;
			}
		} else {
			obuf.x += u_char_width(u);
		}
	}
	n = obuf.x - obuf.scroll_x;
	if (n) {
		// skipped too much
		if (obuf.alloc - obuf.count < 8)
			buf_flush();
		if (u == '\t') {
			memset(obuf.buf + obuf.count, ' ', n);
			obuf.count += n;
		} else if (u < 0x20) {
			obuf.buf[obuf.count++] = u | 0x40;
		} else if (u & U_INVALID_MASK) {
			if (n > 2)
				obuf.buf[obuf.count++] = hex_tab[(u >> 4) & 0x0f];
			if (n > 1)
				obuf.buf[obuf.count++] = hex_tab[u & 0x0f];
			obuf.buf[obuf.count++] = '>';
		} else {
			obuf.buf[obuf.count++] = '>';
		}
	}
	while (1) {
		unsigned int width, space = obuf.scroll_x + obuf.width - obuf.x;

		BUG_ON(obuf.x > obuf.scroll_x + obuf.width);
		if (!space) {
			screen_next_line(bi);
			return;
		}
		if (!screen_next_char(bi, &u) || u == '\n')
			break;
		if (obuf.alloc - obuf.count < 8)
			buf_flush();

		if (u < 0x80 || !utf8) {
			if (u >= 0x20) {
				obuf.buf[obuf.count++] = u;
				obuf.x++;
			} else if (u == '\t') {
				width = (obuf.x + tw) / tw * tw - obuf.x;
				if (width > space)
					width = space;
				memset(obuf.buf + obuf.count, ' ', width);
				obuf.count += width;
				obuf.x += width;
			} else {
				obuf.buf[obuf.count++] = '^';
				obuf.x++;
				if (space > 1) {
					obuf.buf[obuf.count++] = u | 0x40;
					obuf.x++;
				}
			}
		} else {
			width = u_char_width(u);
			if (width <= space) {
				u_set_char(obuf.buf, &obuf.count, u);
				obuf.x += width;
			} else {
				obuf.buf[obuf.count++] = '>';
				obuf.x++;
			}
		}
	}
	buf_clear_eol();
}

static void update_full(void)
{
	BLOCK_ITER_CURSOR(bi, view);
	int i;

	buf_hide_cursor();
	obuf.scroll_x = view->vx;

	for (i = 0; i < view->cy - view->vy; i++)
		block_iter_prev_line(&bi);
	block_iter_bol(&bi);

	selection_init(&bi);
	for (i = 0; i < window->h; i++) {
		if (bi.offset == bi.blk->size && bi.blk->node.next == bi.head)
			break;
		buf_move_cursor(0, i);
		print_line(&bi);
	}
	selection_check();

	if (i < window->h) {
		// dummy empty line
		buf_move_cursor(0, i++);
		buf_clear_eol();
	}

	for (; i < window->h; i++) {
		buf_move_cursor(0, i);
		buf_ch('~');
		buf_clear_eol();
	}

	obuf.scroll_x = 0;
	print_status_line();
	print_command_line();

	buf_move_cursor(view->cx - view->vx, view->cy - view->vy);
	buf_show_cursor();
}

static void update_cursor_line(void)
{
	BLOCK_ITER_CURSOR(bi, view);

	buf_hide_cursor();
	obuf.scroll_x = view->vx;
	block_iter_bol(&bi);

	selection_init(&bi);
	buf_move_cursor(0, view->cy - view->vy);
	print_line(&bi);
	selection_check();

	obuf.scroll_x = 0;
	print_status_line();
	print_command_line();

	buf_move_cursor(view->cx - view->vx, view->cy - view->vy);
	buf_show_cursor();
}

static void update_status_line(void)
{
	buf_hide_cursor();

	print_status_line();
	print_command_line();

	buf_move_cursor(view->cx - view->vx, view->cy - view->vy);
	buf_show_cursor();
}

static void update_window_sizes(void)
{
	int w, h;

	if (!term_get_size(&w, &h) && w > 2 && h > 2) {
		window->w = w;
		window->h = h - 2;
		obuf.width = w;
	}
}

static void debug_blocks(void)
{
	struct block *blk;
	unsigned int count = 0;

	list_for_each_entry(blk, &buffer->blocks, node)
		count++;
	BUG_ON(!count);

	list_for_each_entry(blk, &buffer->blocks, node) {
		unsigned int nl;

		if (count > 1)
			BUG_ON(!blk->size);

		BUG_ON(blk->size > blk->alloc);
		nl = count_nl(blk->data, blk->size);
		BUG_ON(nl != blk->nl);
		if (blk == view->cblk) {
			BUG_ON(view->coffset > blk->size);
		}
	}
}

void ui_start(void)
{
	term_raw();

	// turn keypad on (makes cursor keys work)
	if (term_cap.ks)
		buf_escape(term_cap.ks);

	// use alternate buffer if possible
/* 	term_write_str("\033[?47h"); */
	if (term_cap.ti)
		buf_escape(term_cap.ti);

	update_window_sizes();
	update_cursor(view);
	update_full();
	buf_flush();
}

void ui_end(void)
{
	// back to main buffer
/* 	term_write_str("\033[?47l"); */
	if (term_cap.te)
		buf_escape(term_cap.te);

	buf_move_cursor(0, window->h + 1);
	buf_ch('\n');
	buf_clear_eol();
	buf_show_cursor();

	// turn keypad off
	if (term_cap.ke)
		buf_escape(term_cap.ke);

	buf_flush();
	term_cooked();
}

static void handle_key(enum term_key_type type, unsigned int key)
{
	struct change_head *save_change_head = buffer->save_change_head;
	int cx = view->cx;
	int cy = view->cy;
	int vx = view->vx;
	int vy = view->vy;

	if (uncompleted_binding) {
		handle_binding(type, key);
	} else {
		switch (input_mode) {
		case INPUT_NORMAL:
			switch (type) {
			case KEY_NORMAL:
				if (key < 0x20 && key != '\t' && key != '\r') {
					handle_binding(type, key);
				} else {
					if (key == '\r') {
						insert_ch('\n');
					} else {
						insert_ch(key);
					}
				}
				break;
			case KEY_META:
				handle_binding(type, key);
				break;
			case KEY_SPECIAL:
				if (key == SKEY_DELETE) {
					delete_ch();
				} else if (key == SKEY_BACKSPACE) {
					backspace();
				} else {
					handle_binding(type, key);
				}
				break;
			}
			break;
		}
	}

	debug_blocks();
	update_cursor(view);

	if (vx != view->vx || vy != view->vy) {
		update_flags |= UPDATE_FULL;
	} else if (cx != view->cx || cy != view->cy ||
			save_change_head != buffer->save_change_head) {
		update_flags |= UPDATE_STATUS_LINE;

		// full update when selecting and cursor moved
		if (view->sel_blk)
			update_flags |= UPDATE_FULL;
	}

	if (update_flags & UPDATE_FULL) {
		update_full();
	} else if (update_flags & UPDATE_CURSOR_LINE) {
		update_cursor_line();
	} else if (update_flags & UPDATE_STATUS_LINE) {
		update_status_line();
	}
	update_flags = 0;

	buf_flush();
}

static void handle_signal(void)
{
	switch (received_signal) {
	case SIGWINCH:
		update_window_sizes();
		update_cursor(view);
		update_full();
		buf_flush();
		break;
	case SIGINT:
		handle_key(KEY_NORMAL, 0x03);
		break;
	case SIGTSTP:
		ui_end();
		kill(0, SIGSTOP);
	case SIGCONT:
		ui_start();
		break;
	}
	received_signal = 0;
}

static void set_signal_handler(int signum, void (*handler)(int))
{
	struct sigaction act;
	sigfillset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = handler;
	sigaction(signum, &act, NULL);
}

static void signal_handler(int signum)
{
	received_signal = signum;
}

int main(int argc, char *argv[])
{
	int i;
	unsigned int flags = TERM_USE_TERMCAP | TERM_USE_TERMINFO;
	const char *term = NULL;
	const char *charset;

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
		if (strcmp(opt, "-m") == 0) {
			flags |= TERM_ESC_META;
			continue;
		}
		if (strcmp(opt, "--help") == 0) {
			return 0;
		}
		if (strncmp(opt, "-t=", 3) == 0) {
			term = opt + 3;
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

	setlocale(LC_CTYPE, "");
	charset = nl_langinfo(CODESET);
	if (strcmp(charset, "UTF-8") == 0)
		flags |= TERM_UTF8;
	if (term_init(term, flags))
		return 1;

	window = window_new();
	if (argc - i) {
		for (; i < argc; i++)
			open_buffer(argv[i]);
	}
	if (list_empty(&window->views))
		open_buffer(NULL);
	set_view(VIEW(window->views.next));

	set_signal_handler(SIGWINCH, signal_handler);
	set_signal_handler(SIGINT, signal_handler);
	set_signal_handler(SIGTSTP, signal_handler);
	set_signal_handler(SIGCONT, signal_handler);
	set_signal_handler(SIGQUIT, SIG_IGN);

	obuf.alloc = 8192;
	obuf.buf = xmalloc(obuf.alloc);
	obuf.width = 80;
	obuf.bg = -1;

	read_config();
	ui_start();

	while (running) {
		if (received_signal) {
			handle_signal();
		} else {
			unsigned int key;
			enum term_key_type type;
			if (term_read_key(&key, &type))
				handle_key(type, key);
		}
	}
	ui_end();
	return 0;
}
