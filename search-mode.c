#include "modes.h"
#include "cmdline.h"
#include "history.h"
#include "editor.h"
#include "search.h"
#include "options.h"

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

static void search_mode_keypress(enum term_key_type type, unsigned int key)
{
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
}

const struct editor_mode_ops search_mode_ops = {
	.keypress = search_mode_keypress,
	.update = normal_update,
};
