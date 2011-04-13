#include "editor.h"
#include "buffer.h"
#include "window.h"
#include "term.h"
#include "obuf.h"
#include "cmdline.h"
#include "search.h"
#include "screen.h"
#include "config.h"
#include "command.h"
#include "input.h"
#include "input-special.h"

enum editor_status editor_status;
enum input_mode input_mode;
enum input_special input_special;
char *home_dir;
int child_controls_terminal;
int resized;
int nr_errors;

static int msg_is_error;
static char error_buf[256];

static int cmdline_x;

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

void any_key(void)
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

static void update_current_window(void)
{
	update_cursor_x();
	update_cursor_y();
	update_view();
	update_term_title();
	if (options.show_tab_bar)
		print_tabbar();
	if (options.show_line_numbers)
		update_line_numbers(window, 1);
	update_range(view->vy, view->vy + window->edit_h);
	update_status_line(format_misc_status());
}

static void restore_cursor(void)
{
	switch (input_mode) {
	case INPUT_NORMAL:
		buf_move_cursor(
			window->edit_x + view->cx_display - view->vx,
			window->edit_y + view->cy - view->vy);
		break;
	case INPUT_COMMAND:
	case INPUT_SEARCH:
		buf_move_cursor(cmdline_x, screen_h - 1);
		break;
	}
}

static void start_update(void)
{
	buf_hide_cursor();
}

static void end_update(void)
{
	restore_cursor();
	buf_show_cursor();
	buf_flush();

	update_flags = 0;
	changed_line_min = INT_MAX;
	changed_line_max = -1;
}

void resize(void)
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

	start_update();
	update_current_window();
	update_command_line();
	end_update();
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

const char *editor_file(const char *name)
{
	return ssprintf("%s/.%s/%s", home_dir, program, name);
}

void clear_error(void)
{
	error_buf[0] = 0;
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
	mark_command_line_changed();
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
	mark_command_line_changed();
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

	start_update();
	update_current_window();
	update_command_line();
	end_update();

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
	clear_error();
	return key;
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
			mark_all_lines_changed();
		} else {
			// Because of trailing whitespace highlighting and
			// highlighting current line in different color
			// the lines cy (old cursor y) and view->cy need
			// to be updated.
			//
			// Always update at least current line.
			lines_changed(cy, view->cy);
		}
		if (is_modified != buffer_modified(buffer))
			mark_tabbar_changed();
	} else {
		mark_tabbar_changed();
		mark_all_lines_changed();
	}

	start_update();
	if (update_flags & UPDATE_ALL_WINDOWS)
		update_window_sizes();
	if (update_flags & UPDATE_TAB_BAR)
		update_term_title();
	if (update_flags & UPDATE_TAB_BAR && options.show_tab_bar)
		print_tabbar();
	if (options.show_line_numbers)
		update_line_numbers(window, update_flags & UPDATE_VIEW);
	if (update_flags & UPDATE_VIEW) {
		update_range(view->vy, view->vy + window->edit_h);
	} else  {
		int y1 = changed_line_min;
		int y2 = changed_line_max;
		if (y1 < view->vy)
			y1 = view->vy;
		if (y2 > view->vy + window->edit_h - 1)
			y2 = view->vy + window->edit_h - 1;
		update_range(y1, y2 + 1);
	}
	update_status_line(format_misc_status());
	if (update_flags & UPDATE_COMMAND_LINE)
		update_command_line();
	end_update();
}

void main_loop(void)
{
	while (editor_status == EDITOR_RUNNING) {
		if (resized) {
			resize();
		} else {
			unsigned int key;
			enum term_key_type type;
			if (term_read_key(&key, &type)) {
				/* clear possible error message */
				if (error_buf[0]) {
					clear_error();
					mark_command_line_changed();
				}
				handle_key(type, key);
			}
		}
	}
}
