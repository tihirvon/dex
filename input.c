#include "input.h"
#include "buffer.h"
#include "edit.h"
#include "change.h"
#include "cmdline.h"
#include "history.h"
#include "editor.h"
#include "search.h"
#include "bind.h"
#include "command.h"
#include "completion.h"
#include "input-special.h"

static int search_pos = -1;
static char *search_text;

static void reset_history_search(void)
{
	search_pos = -1;
}

static void insert_paste(void)
{
	unsigned int size;
	char *text = term_read_paste(&size);

	// because this is not a command (see run_command()) you have to
	// call begin_change() to avoid merging this change into previous
	begin_change(CHANGE_MERGE_NONE);
	insert_text(text, size);
	end_change();

	free(text);
}

static void cmdline_insert_paste(void)
{
	unsigned int size;
	char *text = term_read_paste(&size);
	cmdline_insert_bytes(text, size);
	free(text);
}

static int common_key(struct ptr_array *history, enum term_key_type type, unsigned int key)
{
	switch (type) {
	case KEY_NORMAL:
		switch (key) {
		case CTRL('['): // ESC
		case CTRL('C'):
			cmdline_clear();
			input_mode = INPUT_NORMAL;
			// clear possible parse error
			clear_error();
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
			if (search_pos < 0) {
				free(search_text);
				search_text = xstrdup(cmdline.buffer);
				search_pos = history->count;
			}
			if (history_search_forward(history, &search_pos, search_text))
				cmdline_set_text(history->ptrs[search_pos]);
			return 1;
		case SKEY_DOWN:
			if (search_pos < 0)
				return 1;
			if (history_search_backward(history, &search_pos, search_text)) {
				cmdline_set_text(history->ptrs[search_pos]);
			} else {
				cmdline_set_text(search_text);
				search_pos = -1;
			}
			return 1;
		default:
			return 0;
		}
		break;
	case KEY_PASTE:
		cmdline_insert_paste();
		break;
	}
	reset_history_search();
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
	history_add(&command_history, cmdline.buffer, command_history_size);
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
	reset_history_search();
}

static void search_mode_key(enum term_key_type type, unsigned int key)
{
	switch (type) {
	case KEY_NORMAL:
		switch (key) {
		case '\r':
			if (cmdline.buffer[0]) {
				search_set_regexp(cmdline.buffer);
				search_next();
				history_add(&search_history, cmdline.buffer, search_history_size);
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
		reset_history_search();
		break;
	case KEY_META:
		switch (key) {
		case 'c':
			options.case_sensitive_search = (options.case_sensitive_search + 1) % 3;
			break;
		case 'r':
			search_set_direction(current_search_direction() ^ 1);
			break;
		}
		break;
	case KEY_SPECIAL:
		break;
	case KEY_PASTE:
		break;
	}
}

void keypress(enum term_key_type type, unsigned int key)
{
	if (input_special) {
		special_input_keypress(type, key);
		return;
	}

	if (nr_pressed_keys()) {
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
		mark_command_line_changed();
		break;
	case INPUT_SEARCH:
		if (!common_key(&search_history, type, key))
			search_mode_key(type, key);
		mark_command_line_changed();
		break;
	}
}
