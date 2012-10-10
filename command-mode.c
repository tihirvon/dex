#include "modes.h"
#include "cmdline.h"
#include "history.h"
#include "editor.h"
#include "command.h"
#include "completion.h"
#include "error.h"

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
}

static void command_mode_keypress(enum term_key_type type, unsigned int key)
{
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
}

const struct editor_mode_ops command_mode_ops = {
	.keypress = command_mode_keypress,
	.update = normal_update,
};
