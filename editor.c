#include "editor.h"
#include "buffer.h"
#include "window.h"
#include "term.h"
#include "obuf.h"
#include "cmdline.h"
#include "completion.h"
#include "search.h"
#include "history.h"
#include "file-history.h"
#include "screen.h"
#include "bind.h"
#include "alias.h"
#include "config.h"
#include "command.h"
#include "change.h"
#include "state.h"
#include "input-special.h"

#include <locale.h>
#include <langinfo.h>
#include <signal.h>

// control key
#define CTRL(x) ((x) & ~0x40)

enum editor_status editor_status;
enum input_mode input_mode;
enum input_special input_special;
char *home_dir;
int child_controls_terminal;

static int resized;

static int nr_errors;
static int msg_is_error;
static char error_buf[256];

static int cmdline_x;

static const char *builtin_rc =
// obvious bindings
"bind left left\n"
"bind right right\n"
"bind up up\n"
"bind down down\n"
"bind home bol\n"
"bind end eol\n"
"bind pgup pgup\n"
"bind pgdown pgdown\n"
"bind delete delete\n"
"bind ^\\[ unselect\n"
"bind ^Z suspend\n"
// backspace is either ^? or ^H
"bind ^\\? erase\n"
"bind ^H erase\n"
// there must be a way to get to the command line
"bind ^C command\n"
// these colors are assumed to exist
"hi default\n"
"hi currentline keep keep keep\n"
"hi selection keep gray keep\n"
"hi statusline black gray\n"
"hi commandline\n"
"hi errormsg bold red\n"
"hi infomsg bold blue\n"
"hi wserror default yellow\n"
"hi nontext blue keep\n"
"hi tabbar black gray\n"
"hi activetab bold\n"
"hi inactivetab black gray\n"
// must initialize string options
"set statusline-left \" %f%s%m%r%s%M\"\n"
"set statusline-right \" %y,%X   %c %C   %E %n %t   %p \"\n";

static void sanity_check(void)
{
	struct block *blk;

	if (!DEBUG)
		return;

	list_for_each_entry(blk, &buffer->blocks, node) {
		if (blk == view->cursor.blk) {
			BUG_ON(view->cursor.offset > view->cursor.blk->size);
			return;
		}
	}
	BUG("cursor not seen\n");
}

void discard_paste(void)
{
	unsigned int size;
	char *text = term_read_paste(&size);
	free(text);
}

static void insert_paste(void)
{
	unsigned int size;
	char *text = term_read_paste(&size);
	unsigned int skip = size;

	if (text[size - 1] != '\n' && block_iter_is_eof(&view->cursor)) {
		xrenew(text, ++size);
		text[size - 1] = '\n';
	}

	begin_change(CHANGE_MERGE_NONE);
	insert(text, size);
	end_change();

	block_iter_skip_bytes(&view->cursor, skip);
	update_preferred_x();
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

		fill_selection_info(&info);
		if (view->selection == SELECT_LINES) {
			snprintf(misc_status, sizeof(misc_status), "[%d lines]", info.nr_lines);
		} else {
			snprintf(misc_status, sizeof(misc_status), "[%d chars]", info.nr_chars);
		}
	} else {
		misc_status[0] = 0;
	}
	return misc_status;
}

static void update_command_line(void)
{
	char prefix = ':';

	buf_reset(0, screen_w, 0);
	buf_move_cursor(0, screen_h - 1);
	switch (input_mode) {
	case INPUT_NORMAL:
		print_message(error_buf, msg_is_error);
		break;
	case INPUT_SEARCH:
		prefix = current_search_direction() == SEARCH_FWD ? '/' : '?';
		// fallthrough
	case INPUT_COMMAND:
		cmdline_x = print_command(prefix);
		break;
	}
	buf_clear_eol();
}

static void update_term_title(void)
{
	print_term_title(ssprintf("%s %c %s",
		buffer->filename ? buffer->filename : "(No name)",
		buffer_modified(buffer) ? '+' : '-',
		program));
}

static void update_full(void)
{
	update_cursor_x();
	update_cursor_y();
	update_view();
	update_term_title();
	if (options.show_tab_bar)
		print_tabbar();
	update_range(view->vy, view->vy + window->h);
	update_status_line(format_misc_status());
	update_command_line();
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

static void resize(void)
{
	resized = 0;
	update_screen_size();

	// "dtach -r winch" sends SIGWINCH after program has been attached
	if (term_cap.strings[STR_CAP_CMD_ks]) {
		// turn keypad on (makes cursor keys work)
		buf_escape(term_cap.strings[STR_CAP_CMD_ks]);
	}
	if (term_cap.strings[STR_CAP_CMD_ti]) {
		// use alternate buffer if possible
		buf_escape(term_cap.strings[STR_CAP_CMD_ti]);
	}

	buf_hide_cursor();
	update_full();
	restore_cursor();
	buf_show_cursor();
	buf_flush();

	update_flags = 0;
}

void ui_start(int prompt)
{
	if (editor_status == EDITOR_INITIALIZING)
		return;

	term_raw();
	if (prompt)
		any_key();
	resize();
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
	if (term_cap.strings[STR_CAP_CMD_te])
		buf_escape(term_cap.strings[STR_CAP_CMD_te]);

	// turn keypad off
	if (term_cap.strings[STR_CAP_CMD_ke])
		buf_escape(term_cap.strings[STR_CAP_CMD_ke]);

	buf_flush();
	term_cooked();
}

const char *ssprintf(const char *format, ...)
{
	static char buf[1024];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	return buf;
}

const char *editor_file(const char *name)
{
	return ssprintf("%s/.%s/%s", home_dir, program, name);
}

void error_msg(const char *format, ...)
{
	va_list ap;
	int pos = 0;

	// some implementations of *printf return -1 if output was truncated
	if (config_file) {
		snprintf(error_buf, sizeof(error_buf), "%s:%d: ", config_file, config_line);
		pos = strlen(error_buf);
		if (current_command) {
			snprintf(error_buf + pos, sizeof(error_buf) - pos,
				"%s: ", current_command->name);
			pos += strlen(error_buf + pos);
		}
	}

	va_start(ap, format);
	vsnprintf(error_buf + pos, sizeof(error_buf) - pos, format, ap);
	va_end(ap);

	msg_is_error = 1;
	update_flags |= UPDATE_COMMAND_LINE;
	nr_errors++;

	if (editor_status == EDITOR_INITIALIZING)
		fprintf(stderr, "%s\n", error_buf);
}

void info_msg(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vsnprintf(error_buf, sizeof(error_buf), format, ap);
	va_end(ap);
	msg_is_error = 0;
	update_flags |= UPDATE_COMMAND_LINE;
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

			if (key == CTRL('C')) {
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
			resize();
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
		case CTRL('['): // ESC
		case CTRL('C'):
			cmdline_clear();
			input_mode = INPUT_NORMAL;
			// clear possible parse error
			error_buf[0] = 0;
			break;
		case CTRL('D'):
			cmdline_delete();
			break;
		case CTRL('H'):
		case 0x7f: // ^?
			if (cmdline.len) {
				cmdline_backspace();
			} else {
				input_mode = INPUT_NORMAL;
			}
			break;
		case CTRL('U'):
			cmdline_delete_bol();
			break;
		case CTRL('V'):
			input_special = INPUT_SPECIAL_UNKNOWN;
			break;
		case CTRL('W'):
			cmdline_erase_word();
			break;

		case CTRL('A'):
			cmdline_pos = 0;
			return 1;
		case CTRL('B'):
			cmdline_prev_char();
			return 1;
		case CTRL('E'):
			cmdline_pos = strlen(cmdline.buffer);
			return 1;
		case CTRL('F'):
			cmdline_next_char();
			return 1;
		case CTRL('Z'):
			ui_end();
			kill(0, SIGSTOP);
			return 1;
		case CTRL('J'): // '\n'
			// not allowed
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
		run_commands(commands, &array);
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
			options.case_sensitive_search = (options.case_sensitive_search + 1) % 3;
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

static void keypress(enum term_key_type type, unsigned int key)
{
	if (input_special) {
		special_input_keypress(type, key);
		return;
	}

	if (nr_pressed_keys) {
		handle_binding(type, key);
		return;
	}

	switch (input_mode) {
	case INPUT_NORMAL:
		switch (type) {
		case KEY_NORMAL:
			if (key == '\t') {
				insert_ch('\t');
			} else if (key == '\r') {
				insert_ch('\n');
			} else if (key < 0x20 || key == 0x7f) {
				handle_binding(type, key);
			} else {
				insert_ch(key);
			}
			break;
		case KEY_META:
		case KEY_SPECIAL:
			handle_binding(type, key);
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
		update_flags |= UPDATE_COMMAND_LINE;
		break;
	case INPUT_SEARCH:
		if (!common_key(&search_history, type, key))
			search_mode_key(type, key);
		update_flags |= UPDATE_COMMAND_LINE;
		break;
	}
}

static void handle_key(enum term_key_type type, unsigned int key)
{
	int is_modified = buffer_modified(buffer);
	int id = buffer->id;
	int cy = view->cy;
	int vx = view->vx;
	int vy = view->vy;

	keypress(type, key);
	sanity_check();
	update_cursor_x();
	update_cursor_y();
	update_view();

	if (id == buffer->id) {
		if (vx != view->vx || vy != view->vy) {
			update_flags |= UPDATE_FULL;
		} else if (cy != view->cy) {
			// Because of trailing whitespace highlighting and
			// highlighting current line in different color
			// the lines cy (old cursor y) and view->cy need
			// to be updated.
			lines_changed(cy, view->cy);
		} else {
			// Too many things to track for a little gain.
			// Always update at least current line.
			lines_changed(cy, cy);
		}
		if (is_modified != buffer_modified(buffer))
			update_flags |= UPDATE_TAB_BAR;
	} else {
		update_flags |= UPDATE_FULL | UPDATE_TAB_BAR;
	}

	buf_hide_cursor();
	if (update_flags & UPDATE_TAB_BAR) {
		update_window_sizes();
		update_term_title();
	}
	if (update_flags & UPDATE_TAB_BAR && options.show_tab_bar)
		print_tabbar();
	if (update_flags & UPDATE_FULL) {
		update_range(view->vy, view->vy + window->h);
	} else  {
		int y1 = changed_line_min;
		int y2 = changed_line_max;
		if (y1 < view->vy)
			y1 = view->vy;
		if (y2 > view->vy + window->h - 1)
			y2 = view->vy + window->h - 1;
		update_range(y1, y2 + 1);
	}
	update_status_line(format_misc_status());
	if (update_flags & UPDATE_COMMAND_LINE)
		update_command_line();
	restore_cursor();
	buf_show_cursor();
	buf_flush();

	update_flags = 0;
	changed_line_min = INT_MAX;
	changed_line_max = -1;
}

void set_signal_handler(int signum, void (*handler)(int))
{
	struct sigaction act;

	clear(&act);
	sigemptyset(&act.sa_mask);
	act.sa_handler = handler;
	sigaction(signum, &act, NULL);
}

static void handle_sigtstp(int signum)
{
	if (!child_controls_terminal)
		ui_end();
	kill(0, SIGSTOP);
}

static void handle_sigcont(int signum)
{
	if (!child_controls_terminal)
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

static const char *opt_arg(const char *opt, const char *arg)
{
	if (arg == NULL) {
		fprintf(stderr, "missing argument for option %s\n", opt);
		exit(1);
	}
	return arg;
}

int main(int argc, char *argv[])
{
	unsigned int flags = TERM_USE_TERMCAP | TERM_USE_TERMINFO;
	const char *home = getenv("HOME");
	const char *tag = NULL;
	const char *rc = NULL;
	const char *command = NULL;
	const char *charset;
	int i, read_rc = 1;

	if (!home)
		home = "";
	home_dir = xstrdup(home);

	for (i = 1; i < argc; i++) {
		const char *opt = argv[i];

		if (opt[0] != '-' || !opt[1])
			break;
		if (!opt[2]) {
			switch (opt[1]) {
			case 'C':
				flags &= ~TERM_USE_TERMCAP;
				continue;
			case 'I':
				flags &= ~TERM_USE_TERMINFO;
				continue;
			case 'R':
				read_rc = 0;
				continue;
			case 't':
				tag = opt_arg(opt, argv[++i]);
				continue;
			case 'r':
				rc = opt_arg(opt, argv[++i]);
				continue;
			case 'c':
				command = opt_arg(opt, argv[++i]);
				continue;
			case 'V':
				printf("%s %s\nWritten by Timo Hirvonen\n", program, version);
				return 0;
			}
			if (opt[1] == '-') {
				i++;
				break;
			}
		}
		printf("Usage: %s [-R] [-V] [-c command] [-t tag] [-r rcfile] [file]...\n", argv[0]);
		return 1;
	}

	// create this early. needed if lock-files is true
	mkdir(editor_file(""), 0755);

	setlocale(LC_CTYPE, "");
	charset = nl_langinfo(CODESET);
	if (strcmp(charset, "UTF-8") == 0)
		flags |= TERM_UTF8;

	if (term_init(NULL, flags))
		error_msg("No terminal entry found.");

	exec_config(commands, builtin_rc, strlen(builtin_rc));
	config_line = 0;
	set_basic_colors();

	window = window_new();
	update_screen_size();

	if (read_rc) {
		if (rc) {
			read_config(commands, rc, 1);
		} else if (read_config(commands, editor_file("rc"), 0)) {
			read_config(commands, ssprintf("%s/rc", pkgdatadir), 1);
		}
	}

	update_all_syntax_colors();
	sort_aliases();

	/* Terminal does not generate signals for control keys. */
	set_signal_handler(SIGINT, SIG_IGN);
	set_signal_handler(SIGQUIT, SIG_IGN);
	set_signal_handler(SIGPIPE, SIG_IGN);

	/* Terminal does not generate signal for ^Z but someone can send
	 * us SIGSTOP or SIGTSTP nevertheless.
	 */
	set_signal_handler(SIGTSTP, handle_sigtstp);

	set_signal_handler(SIGCONT, handle_sigcont);
	set_signal_handler(SIGWINCH, handle_sigwinch);

	obuf.alloc = 8192;
	obuf.buf = xmalloc(obuf.alloc);

	load_file_history();
	history_load(&command_history, editor_file("command-history"));
	history_load(&search_history, editor_file("search-history"));

	/* Initialize terminal but don't update screen yet.  Also display
	 * "Press any key to continue" prompt if there were any errors
	 * during reading configuration files.
	 */
	term_raw();
	if (nr_errors) {
		any_key();
		error_buf[0] = 0;
	}

	editor_status = EDITOR_RUNNING;

	for (; i < argc; i++)
		open_buffer(argv[i], 0);
	if (list_empty(&window->views))
		open_empty_buffer();
	set_view(VIEW(window->views.next));

	if (command || tag)
		resize();

	if (command)
		handle_command(commands, command);
	if (tag) {
		const char *ptrs[3] = { "tag", tag, NULL };
		struct ptr_array array = { (void **)ptrs, 3, 3 };
		run_commands(commands, &array);
	}
	resize();

	while (editor_status == EDITOR_RUNNING) {
		if (resized) {
			resize();
		} else {
			unsigned int key;
			enum term_key_type type;
			if (term_read_key(&key, &type)) {
				/* clear possible error message */
				if (error_buf[0]) {
					error_buf[0] = 0;
					update_flags |= UPDATE_COMMAND_LINE;
				}
				handle_key(type, key);
			}
		}
	}
	ui_end();
	history_save(&command_history, editor_file("command-history"));
	history_save(&search_history, editor_file("search-history"));
	close_all_views();
	save_file_history();
	return 0;
}
