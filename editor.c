#include "buffer.h"
#include "term.h"
#include "obuf.h"

#include <locale.h>
#include <langinfo.h>
#include <signal.h>

static int running = 1;
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

static const char *ssprintf(const char *format, ...)
{
	static char buf[256];
	va_list ap;

	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	return buf;
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
				if (buffer->save_change_head != buffer->cur_change_head)
					w += add_status_str(buf, size, &pos, "[+]");
				break;
			case 'y':
				w += add_status_str(buf, size, &pos, ssprintf("%d", window->cy));
				break;
			case 'x':
				w += add_status_str(buf, size, &pos, ssprintf("%d", window->cx));
				break;
			case 'X':
				w += add_status_str(buf, size, &pos, ssprintf("%d", window->cx_idx));
				if (window->cx != window->cx_idx)
					w += add_status_str(buf, size, &pos, ssprintf("-%d", window->cx));
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

	buf_move_cursor(0, window->h - 2);
	buf_set_colors(-1, 7);
	lw = format_status(lbuf, sizeof(lbuf), lformat);
	rw = format_status(rbuf, sizeof(rbuf), rformat);
	if (lw + rw <= window->w) {
		buf_add_bytes(lbuf, strlen(lbuf));
		buf_set_bytes(' ', window->w - lw - rw);
		buf_add_bytes(rbuf, strlen(rbuf));
	} else {
		buf_add_bytes(lbuf, strlen(lbuf));
		buf_move_cursor(window->w - rw, window->h - 2);
		buf_add_bytes(rbuf, strlen(rbuf));
	}
}

static void print_command_line(void)
{
	buf_move_cursor(0, window->h - 1);
	buf_set_colors(-1, -1);
	buf_clear_eol();
}

static void print_line(struct block_iter *bi)
{
	int n, tw = buffer->tab_width;
	int utf8 = buffer->utf8;
	uchar u;

	while (obuf.x < obuf.scroll_x) {
		if (!buffer->next_char(bi, &u) || u == '\n') {
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
			block_iter_next_line(bi);
			return;
		}
		if (!buffer->next_char(bi, &u) || u == '\n')
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
	BLOCK_ITER_CURSOR(bi, window);
	int i;

	buf_hide_cursor();
	obuf.scroll_x = window->vx;

	for (i = 0; i < window->cy - window->vy; i++)
		block_iter_prev_line(&bi);
	block_iter_bol(&bi);

	for (i = 0; i < window->h - 2; i++) {
		if (bi.offset == bi.blk->size && bi.blk->node.next == bi.head)
			break;
		buf_move_cursor(0, i);
		print_line(&bi);
	}
	if (i < window->h - 2) {
		// dummy empty line
		buf_move_cursor(0, i++);
		buf_clear_eol();
	}

	for (; i < window->h - 2; i++) {
		buf_move_cursor(0, i);
		buf_ch('~');
		buf_clear_eol();
	}

	obuf.scroll_x = 0;
	print_status_line();
	print_command_line();

	buf_move_cursor(window->cx - window->vx, window->cy - window->vy);
	buf_show_cursor();
}

static void update_cursor_line(void)
{
	BLOCK_ITER_CURSOR(bi, window);

	buf_hide_cursor();
	obuf.scroll_x = window->vx;
	block_iter_bol(&bi);

	buf_move_cursor(0, window->cy - window->vy);
	print_line(&bi);

	obuf.scroll_x = 0;
	print_status_line();
	print_command_line();

	buf_move_cursor(window->cx - window->vx, window->cy - window->vy);
	buf_show_cursor();
}

static void update_status_line(void)
{
	buf_hide_cursor();

	print_status_line();
	print_command_line();

	buf_move_cursor(window->cx - window->vx, window->cy - window->vy);
	buf_show_cursor();
}

static void update_window_sizes(void)
{
	int w, h;

	if (!term_get_size(&w, &h) && w > 2 && h > 2) {
		window->w = w;
		window->h = h;
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
		if (blk == window->cblk) {
			BUG_ON(window->coffset > blk->size);
		}
	}
}

static void debug_contents(void)
{
	struct block *blk;

	write(2, "\n--\n", 4);
	list_for_each_entry(blk, &buffer->blocks, node) {
		write(2, blk->data, blk->size);
		if (blk->node.next != &buffer->blocks)
			write(2, "-X-", 3);
	}
	write(2, "--\n", 3);
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
	update_cursor(window);
	update_full();
	buf_flush();
}

void ui_end(void)
{
	// back to main buffer
/* 	term_write_str("\033[?47l"); */
	if (term_cap.te)
		buf_escape(term_cap.te);

	buf_move_cursor(0, window->h - 1);
	buf_ch('\n');
	buf_clear_eol();
	buf_show_cursor();

	// turn keypad off
	if (term_cap.ke)
		buf_escape(term_cap.ke);

	buf_flush();
	term_cooked();
}

static void handle_special(int key)
{
	if (key == SKEY_DELETE) {
		delete_ch();
		return;
	}
	if (key == SKEY_BACKSPACE) {
		backspace();
		return;
	}

	undo_merge = UNDO_MERGE_NONE;
	switch (key) {
	case SKEY_LEFT:
		move_left(1);
		break;
	case SKEY_RIGHT:
		move_right(1);
		break;
	case SKEY_HOME:
		move_bol();
		break;
	case SKEY_END:
		move_eol();
		break;
	case SKEY_UP:
		move_up(1);
		break;
	case SKEY_DOWN:
		move_down(1);
		break;
	case SKEY_PAGE_UP:
		move_up(window->h - 3);
		break;
	case SKEY_PAGE_DOWN:
		move_down(window->h - 3);
		break;
	case SKEY_F2:
		save_buffer();
		break;
	case SKEY_F9:
		running = 0;
		break;
	case SKEY_F5:
		prev_buffer();
		break;
	case SKEY_F6:
		next_buffer();
		break;
	}
}

static void handle_key(enum term_key_type type, unsigned int key)
{
	struct change_head *save_change_head = buffer->save_change_head;
	int cx = window->cx;
	int cy = window->cy;
	int vx = window->vx;
	int vy = window->vy;

	switch (type) {
	case KEY_INVALID:
		break;
	case KEY_NONE:
		break;
	case KEY_NORMAL:
		if (key < 0x20 && key != '\t' && key != '\r') {
			switch (key) {
			case 0x03: // ^C
				debug_contents();
				break;
			case 0x04: // ^D
				delete_ch();
				break;
			case 0x05: // ^E
				undo();
				break;
			case 0x12: // ^R
				redo();
				break;
			case 0x10: // ^P
				paste();
				break;
			case 0x19: // ^Y
				copy_line();
				break;
			case 0x0b: // ^K
				cut_line();
				break;
			}
		} else {
			if (key == '\r') {
				insert_ch('\n');
			} else {
				insert_ch(key);
			}
		}
		break;
	case KEY_META:
		break;
	case KEY_SPECIAL:
		handle_special(key);
		break;
	}

	debug_blocks();
	update_cursor(window);

	if (vx != window->vx || vy != window->vy) {
		update_flags |= UPDATE_FULL;
	} else if (cx != window->cx || cy != window->cy ||
			save_change_head != buffer->save_change_head) {
		update_flags |= UPDATE_STATUS_LINE;
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
		update_cursor(window);
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
			buffer_load(argv[i]);
	}
	if (!window->buffers)
		buffer_new_file();
	set_buffer(window->buffers[0]);

	set_signal_handler(SIGWINCH, signal_handler);
	set_signal_handler(SIGINT, signal_handler);
	set_signal_handler(SIGTSTP, signal_handler);
	set_signal_handler(SIGCONT, signal_handler);
	set_signal_handler(SIGQUIT, SIG_IGN);

	obuf.alloc = 8192;
	obuf.buf = xmalloc(obuf.alloc);
	obuf.width = 80;
	obuf.bg = -1;
	ui_start();
	while (running) {
		if (received_signal) {
			handle_signal();
		} else {
			unsigned int key;
			enum term_key_type type;
			type = term_read_key(&key);
			handle_key(type, key);
		}
	}
	ui_end();
	return 0;
}
