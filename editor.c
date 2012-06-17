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
#include "modes.h"
#include "error.h"

enum editor_status editor_status;
enum input_mode input_mode;
CMDLINE(cmdline);
char *home_dir;
char *charset;
int child_controls_terminal;
int resized;

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

void any_key(void)
{
	unsigned int key;
	enum term_key_type type;

	printf("Press any key to continue\n");
	while (!term_read_key(&key, &type))
		;
	if (type == KEY_PASTE)
		term_discard_paste();
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

static void update_current_window(void)
{
	update_cursor_x();
	update_cursor_y();
	update_view();
	if (options.show_tab_bar)
		print_tabbar();
	if (options.show_line_numbers)
		update_line_numbers(window, 1);
	update_range(view->vy, view->vy + window->edit_h);
	update_status_line();
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
	int i;

	restore_cursor();
	buf_show_cursor();
	buf_flush();

	buffer->changed_line_min = INT_MAX;
	buffer->changed_line_max = -1;
	for (i = 0; i < windows.count; i++)
		WINDOW(i)->update_tabbar = 0;
}

static void update_all_windows(void)
{
	struct window *save = window;
	int i;

	update_window_sizes();
	for (i = 0; i < windows.count; i++) {
		window = WINDOW(i);
		view = window->view;
		buffer = view->buffer;
		update_current_window();
	}
	update_separators();
	window = save;
	view = window->view;
	buffer = view->buffer;
}

static void update_window(void)
{
	int y1, y2;

	if (window->update_tabbar && options.show_tab_bar)
		print_tabbar();

	if (options.show_line_numbers) {
		// force updating lines numbers if all lines changed
		update_line_numbers(window, buffer->changed_line_max == INT_MAX);
	}

	y1 = buffer->changed_line_min;
	y2 = buffer->changed_line_max;
	if (y1 < view->vy)
		y1 = view->vy;
	if (y2 > view->vy + window->edit_h - 1)
		y2 = view->vy + window->edit_h - 1;

	update_range(y1, y2 + 1);
	update_status_line();
}

// update all visible views containing current buffer
static void update_windows(void)
{
	struct view *save = view;
	void **ptrs = buffer->views.ptrs;
	int i, count = buffer->views.count;

	for (i = 0; i < count; i++) {
		struct view *v = ptrs[i];
		if (v->window->view == v) {
			// visible view
			view = v;
			buffer = view->buffer;
			window = view->window;

			if (view != save) {
				// restore cursor
				view->cursor.blk = BLOCK(view->buffer->blocks.next);
				block_iter_goto_offset(&view->cursor, view->saved_cursor_offset);

				// these have already been updated for current view
				update_cursor_x();
				update_cursor_y();
				update_view();
			}
			update_window();
		}
	}
	view = save;
	buffer = view->buffer;
	window = view->window;
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
	update_term_title();
	update_all_windows();
	update_command_line();
	end_update();
}

void ui_end(void)
{
	struct term_color color = { -1, -1, 0 };

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

void suspend(void)
{
	if (!child_controls_terminal && editor_status != EDITOR_INITIALIZING)
		ui_end();
	kill(0, SIGSTOP);
}

char *editor_file(const char *name)
{
	return xsprintf("%s/.%s/%s", home_dir, program, name);
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
	msg_is_error = 0;

	start_update();
	update_term_title();
	update_current_window();
	update_command_line();
	end_update();

	while (1) {
		enum term_key_type type;

		if (term_read_key(&key, &type)) {
			if (type == KEY_PASTE)
				term_discard_paste();
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

	modes[input_mode].keypress(type, key);
	sanity_check();

	if (everything_changed) {
		start_update();
		update_term_title();
		update_all_windows();
		update_command_line();
		end_update();
		everything_changed = 0;
		return;
	}

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
			mark_buffer_tabbars_changed();
	} else {
		window->update_tabbar = 1;
		mark_all_lines_changed();
	}

	start_update();
	if (window->update_tabbar)
		update_term_title();
	update_windows();
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
				}
				handle_key(type, key);
			}
		}
	}
}
