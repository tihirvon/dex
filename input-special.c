#include "input-special.h"
#include "uchar.h"
#include "common.h"

enum input_special {
	/* not inputting special characters */
	INPUT_SPECIAL_NONE,

	/* not known yet (just started by hitting ^V) */
	INPUT_SPECIAL_UNKNOWN,

	/* accept any value 0-255 (3 octal digits) */
	INPUT_SPECIAL_OCT,

	/* accept any value 0-255 (3 decimal digits) */
	INPUT_SPECIAL_DEC,

	/* accept any value 0-255 (2 hexadecimal digits) */
	INPUT_SPECIAL_HEX,

	/* accept any valid unicode value (6 hexadecimal digits) */
	INPUT_SPECIAL_UNICODE,
};

static enum input_special input_special;
static struct {
	int base;
	int max_chars;
	int value;
	int nr;
} raw_input;

void special_input_activate(void)
{
	input_special = INPUT_SPECIAL_UNKNOWN;
}

int special_input_keypress(enum term_key_type type, unsigned int key, char *buf, int *count)
{
	*count = 0;
	if (input_special == INPUT_SPECIAL_NONE)
		return 0;

	if (type != KEY_NORMAL) {
		if (type == KEY_PASTE)
			term_discard_paste();
		input_special = INPUT_SPECIAL_NONE;
		return 1;
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
				*count = 1;
				input_special = INPUT_SPECIAL_NONE;
				return 1;
			}
			return 1;
		}
	}

	if (key == 0x08 || key == 0x7f) {
		if (raw_input.nr) {
			raw_input.value /= raw_input.base;
			raw_input.nr--;
		}
		return 1;
	}

	if (key != '\r') {
		int n = hex_decode(key);

		if (n < 0 || n >= raw_input.base) {
			input_special = INPUT_SPECIAL_NONE;
			return 1;
		}
		raw_input.value *= raw_input.base;
		raw_input.value += n;
		if (++raw_input.nr < raw_input.max_chars)
			return 1;
	}

	if (input_special == INPUT_SPECIAL_UNICODE && u_is_unicode(raw_input.value)) {
		unsigned int idx = 0;
		u_set_char_raw(buf, &idx, raw_input.value);
		*count = idx;
	}
	if (input_special != INPUT_SPECIAL_UNICODE && raw_input.value <= 255) {
		buf[0] = raw_input.value;
		*count = 1;
	}
	input_special = INPUT_SPECIAL_NONE;
	return 1;
}

int special_input_misc_status(char *status)
{
	int i, value = raw_input.value;
	const char *str = "";
	char buf[7];

	if (input_special == INPUT_SPECIAL_NONE)
		return 0;

	if (input_special == INPUT_SPECIAL_UNKNOWN) {
		strcpy(status, "[Insert special]");
		return 1;
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
	return 1;
}
