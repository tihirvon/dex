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
#include "highlight.h"
#include "screen.h"
#include "bind.h"
#include "alias.h"

#include <locale.h>
#include <langinfo.h>
#include <signal.h>

char *home_dir;

enum input_mode input_mode;
enum input_special input_special;
enum editor_status editor_status;

static int resized;

static void debug_blocks(void)
{
#if DEBUG > 0
	struct block *blk;
	int cursor_seen = 0;

	BUG_ON(list_empty(&buffer->blocks));

	list_for_each_entry(blk, &buffer->blocks, node) {
		BUG_ON(!blk->size && buffer->blocks.next->next != &buffer->blocks);
		BUG_ON(blk->size > blk->alloc);
		BUG_ON(count_nl(blk->data, blk->size) != blk->nl);
		BUG_ON(blk == view->cursor.blk && view->cursor.offset > blk->size);
		BUG_ON(blk->size && blk->data[blk->size - 1] != '\n' && blk->node.next != &buffer->blocks);
		if (blk == view->cursor.blk)
			cursor_seen = 1;
	}
	BUG_ON(!cursor_seen);
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
	if (editor_status == EDITOR_INITIALIZING)
		return;

	term_raw();
	update_terminal_settings();
	if (prompt)
		any_key();
	update_everything();
}

void ui_end(void)
{
	struct term_color color = { -1, -1, 0 };

	if (editor_status == EDITOR_INITIALIZING)
		return;

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

const char *editor_file(const char *name)
{
	static char filename[1024];
	snprintf(filename, sizeof(filename), "%s/.editor/%s", home_dir, name);
	return filename;
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

static void command_line_enter(void)
{
	PTR_ARRAY(array);
	int ret;

	reset_completion();
	input_mode = INPUT_NORMAL;
	ret = parse_commands(&array, cmdline.buffer);

	/* Need to do this before executing the command because
	 * "command" can modify contents of command line.
	 */
	history_add(&command_history, cmdline.buffer);
	cmdline_clear();

	if (!ret)
		run_commands(&array);
	ptr_array_free(&array);
}

static void command_mode_key(enum term_key_type type, unsigned int key)
{
	switch (type) {
	case KEY_NORMAL:
		switch (key) {
		case '\r':
			command_line_enter();
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

	if (!(update_flags & UPDATE_TAB_BAR)) {
		// view has not changed
		if (vx != view->vx || vy != view->vy) {
			update_flags |= UPDATE_FULL;
		} else if (cy != view->cy) {
			update_flags |= UPDATE_RANGE;
		} else if (cx != view->cx_display) {
			update_flags |= UPDATE_CURSOR_LINE;
		}
		if (is_modified != buffer_modified(buffer))
			update_flags |= UPDATE_STATUS_LINE | UPDATE_TAB_BAR;
	}

	if (show_tab_bar != options.show_tab_bar) {
		update_window_sizes();
		update_flags |= UPDATE_FULL | UPDATE_TAB_BAR;
	}

	if (!update_flags)
		return;

	buf_hide_cursor();
	if (update_flags & UPDATE_TAB_BAR && options.show_tab_bar)
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
		update_command_line();
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
		move_offset(buffer_offset() + size);
		update_full();
		break;
	case INPUT_COMMAND:
	case INPUT_SEARCH:
		cmdline_insert_bytes(buf, size);
		update_status_line();
		update_command_line();
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
	update_command_line();
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
		unsigned int idx = 0;
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
	const char *home = getenv("HOME");
	const char *tag = NULL;
	const char *rc = NULL;
	const char *command = NULL;
	const char *charset;
	struct view *tmp_view;

	if (!home)
		home = "";
	home_dir = xstrdup(home);

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

		printf("Usage: %s [-c COMMAND] [-t TAG] [-r RC_FILE] [FILE]...\n", argv[0]);
		return !!strcmp(opt, "--help");
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
		error_msg("No terminal entry found.");

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
