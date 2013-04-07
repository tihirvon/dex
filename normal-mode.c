#include "modes.h"
#include "buffer.h"
#include "view.h"
#include "edit.h"
#include "change.h"
#include "bind.h"
#include "input-special.h"
#include "editor.h"

static void insert_paste(void)
{
	long size;
	char *text = term_read_paste(&size);

	// because this is not a command (see run_command()) you have to
	// call begin_change() to avoid merging this change into previous
	begin_change(CHANGE_MERGE_NONE);
	insert_text(text, size);
	end_change();

	free(text);
}

static void normal_mode_keypress(enum term_key_type type, unsigned int key)
{
	char buf[4];
	int count;

	if (special_input_keypress(type, key, buf, &count)) {
		if (count) {
			begin_change(CHANGE_MERGE_NONE);
			buffer_insert_bytes(buf, count);
			end_change();
			block_iter_skip_bytes(&view->cursor, count);
		}
		return;
	}
	if (nr_pressed_keys()) {
		handle_binding(type, key);
		return;
	}
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
}

const struct editor_mode_ops normal_mode_ops = {
	.keypress = normal_mode_keypress,
	.update = normal_update,
};
