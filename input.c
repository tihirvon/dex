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
#include "error.h"

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

static void command_line_enter(void)
{
	PTR_ARRAY(array);
	int ret;

	reset_completion();
	input_mode = INPUT_NORMAL;
	ret = parse_commands(&array, cmdline.buf.buffer);

	/* Need to do this before executing the command because
	 * "command" can modify contents of command line.
	 */
	history_add(&command_history, cmdline.buf.buffer, command_history_size);
	cmdline_clear(&cmdline);

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
		}
		break;
	case KEY_META:
		return;
	case KEY_SPECIAL:
		return;
	case KEY_PASTE:
		return;
	}
	cmdline_reset_history_search(&cmdline);
}

static void search_mode_key(enum term_key_type type, unsigned int key)
{
	switch (type) {
	case KEY_NORMAL:
		switch (key) {
		case '\r':
			if (cmdline.buf.buffer[0]) {
				search_set_regexp(cmdline.buf.buffer);
				search_next();
				history_add(&search_history, cmdline.buf.buffer, search_history_size);
			} else {
				search_next();
			}
			cmdline_clear(&cmdline);
			input_mode = INPUT_NORMAL;
			break;
		}
		cmdline_reset_history_search(&cmdline);
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
		switch (cmdline_handle_key(&cmdline, &command_history, type, key)) {
		case CMDLINE_UNKNOWN_KEY:
			command_mode_key(type, key);
			break;
		case CMDLINE_KEY_HANDLED:
			reset_completion();
			break;
		case CMDLINE_CANCEL:
			input_mode = INPUT_NORMAL;
			// clear possible parse error
			clear_error();
			break;
		}
		break;
	case INPUT_SEARCH:
		switch (cmdline_handle_key(&cmdline, &search_history, type, key)) {
		case CMDLINE_UNKNOWN_KEY:
			search_mode_key(type, key);
			break;
		case CMDLINE_KEY_HANDLED:
			break;
		case CMDLINE_CANCEL:
			input_mode = INPUT_NORMAL;
			break;
		}
		break;
	}
}
