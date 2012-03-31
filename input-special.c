#include "input-special.h"
#include "editor.h"
#include "buffer.h"
#include "change.h"
#include "cmdline.h"
#include "uchar.h"
#include "edit.h"

static struct {
	int base;
	int max_chars;
	int value;
	int nr;
} raw_input;

static void insert_special(const char *buf, int size)
{
	switch (input_mode) {
	case INPUT_NORMAL:
		begin_change(CHANGE_MERGE_NONE);
		insert(buf, size);
		end_change();
		block_iter_skip_bytes(&view->cursor, size);
		update_preferred_x();
		break;
	case INPUT_COMMAND:
	case INPUT_SEARCH:
		// \n is not allowed in command line because
		// command/search history file would break
		if (buf[0] != '\n')
			cmdline_insert_bytes(&cmdline, buf, size);
		break;
	}
}

void special_input_keypress(enum term_key_type type, unsigned int key)
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

	if (key == 0x08 || key == 0x7f) {
		if (raw_input.nr) {
			raw_input.value /= raw_input.base;
			raw_input.nr--;
		}
		return;
	}

	if (key != '\r') {
		int n = hex_decode(key);

		if (n < 0 || n >= raw_input.base) {
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

void format_input_special_misc_status(char *status)
{
	int i, value = raw_input.value;
	const char *str = "";
	char buf[7];

	if (input_special == INPUT_SPECIAL_UNKNOWN) {
		strcpy(status, "[Insert special]");
		return;
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
		str = "Oct";
		break;
	case INPUT_SPECIAL_DEC:
		str = "Dec";
		break;
	case INPUT_SPECIAL_HEX:
		str = "Hex";
		break;
	case INPUT_SPECIAL_UNICODE:
		str = "Unicode, hex";
		break;
	}

	sprintf(status, "[%s <%s>]", str, buf);
}
