#include "cmdline.h"
#include "gbuf.h"
#include "history.h"
#include "editor.h"
#include "common.h"
#include "uchar.h"
#include "input-special.h"

static void cmdline_insert(struct cmdline *c, unsigned int u)
{
	unsigned int len = u_char_size(u);

	gbuf_make_space(&c->buf, c->pos, len);
	if (len > 1) {
		u_set_char_raw(c->buf.buffer, &c->pos, u);
	} else {
		c->buf.buffer[c->pos++] = u;
	}
}

static void cmdline_delete(struct cmdline *c)
{
	long pos = c->pos;
	long len = 1;

	if (pos == c->buf.len)
		return;

	u_get_char(c->buf.buffer, c->buf.len, &pos);
	len = pos - c->pos;
	gbuf_remove(&c->buf, c->pos, len);
}

static void cmdline_backspace(struct cmdline *c)
{
	if (c->pos) {
		u_prev_char(c->buf.buffer, &c->pos);
		cmdline_delete(c);
	}
}

static void cmdline_erase_word(struct cmdline *c)
{
	int i = c->pos;

	if (!c->pos)
		return;

	// open /path/to/file^W => open /path/to/

	// erase whitespace
	while (i && isspace(c->buf.buffer[i - 1]))
		i--;

	// erase non-word bytes
	while (i && !is_word_byte(c->buf.buffer[i - 1]))
		i--;

	// erase word bytes
	while (i && is_word_byte(c->buf.buffer[i - 1]))
		i--;

	gbuf_remove(&c->buf, i, c->pos - i);
	c->pos = i;
}

static void cmdline_delete_bol(struct cmdline *c)
{
	gbuf_remove(&c->buf, 0, c->pos);
	c->pos = 0;
}

static void cmdline_delete_eol(struct cmdline *c)
{
	c->buf.buffer[c->pos] = 0;
	c->buf.len = c->pos;
}

static void cmdline_prev_char(struct cmdline *c)
{
	if (c->pos)
		u_prev_char(c->buf.buffer, &c->pos);
}

static void cmdline_next_char(struct cmdline *c)
{
	if (c->pos < c->buf.len)
		u_get_char(c->buf.buffer, c->buf.len, &c->pos);
}

static void cmdline_insert_bytes(struct cmdline *c, const char *buf, int size)
{
	int i;

	gbuf_make_space(&c->buf, c->pos, size);
	for (i = 0; i < size; i++)
		c->buf.buffer[c->pos++] = buf[i];
}

static void cmdline_insert_paste(struct cmdline *c)
{
	long size, i;
	char *text = term_read_paste(&size);

	for (i = 0; i < size; i++) {
		if (text[i] == '\n')
			text[i] = ' ';
	}
	cmdline_insert_bytes(c, text, size);
	free(text);
}

static void set_text(struct cmdline *c, const char *text)
{
	gbuf_clear(&c->buf);
	gbuf_add_str(&c->buf, text);
	c->pos = strlen(text);
}

void cmdline_clear(struct cmdline *c)
{
	gbuf_clear(&c->buf);
	c->pos = 0;
	c->search_pos = -1;
}

void cmdline_set_text(struct cmdline *c, const char *text)
{
	set_text(c, text);
	c->search_pos = -1;
}

int cmdline_handle_key(struct cmdline *c, struct ptr_array *history, enum term_key_type type, unsigned int key)
{
	char buf[4];
	int count;

	if (special_input_keypress(type, key, buf, &count)) {
		// \n is not allowed in command line because
		// command/search history file would break
		if (count && buf[0] != '\n')
			cmdline_insert_bytes(&cmdline, buf, count);
		c->search_pos = -1;
		return 1;
	}
	switch (type) {
	case KEY_NORMAL:
		switch (key) {
		case CTRL('['): // ESC
		case CTRL('C'):
			cmdline_clear(c);
			return CMDLINE_CANCEL;
		case CTRL('D'):
			cmdline_delete(c);
			break;
		case CTRL('K'):
			cmdline_delete_eol(c);
			break;
		case CTRL('H'):
		case 0x7f: // ^?
			if (c->buf.len == 0)
				return CMDLINE_CANCEL;
			cmdline_backspace(c);
			break;
		case CTRL('U'):
			cmdline_delete_bol(c);
			break;
		case CTRL('V'):
			special_input_activate();
			break;
		case CTRL('W'):
			cmdline_erase_word(c);
			break;

		case CTRL('A'):
			c->pos = 0;
			return 1;
		case CTRL('B'):
			cmdline_prev_char(c);
			return 1;
		case CTRL('E'):
			c->pos = strlen(c->buf.buffer);
			return 1;
		case CTRL('F'):
			cmdline_next_char(c);
			return 1;
		case CTRL('Z'):
			suspend();
			return 1;
		default:
			// don't insert control characters
			if (key >= 0x20 && key != 0x7f) {
				cmdline_insert(c, key);
				return 1;
			}
			return 0;
		}
		break;
	case KEY_META:
		return 0;
	case KEY_SPECIAL:
		switch (key) {
		case SKEY_DELETE:
			cmdline_delete(c);
			break;

		case SKEY_LEFT:
			cmdline_prev_char(c);
			return 1;
		case SKEY_RIGHT:
			cmdline_next_char(c);
			return 1;
		case SKEY_HOME:
			c->pos = 0;
			return 1;
		case SKEY_END:
			c->pos = strlen(c->buf.buffer);
			return 1;
		case SKEY_UP:
			if (history == NULL)
				return 0;
			if (c->search_pos < 0) {
				free(c->search_text);
				c->search_text = xstrdup(c->buf.buffer);
				c->search_pos = history->count;
			}
			if (history_search_forward(history, &c->search_pos, c->search_text))
				set_text(c, history->ptrs[c->search_pos]);
			return 1;
		case SKEY_DOWN:
			if (history == NULL)
				return 0;
			if (c->search_pos < 0)
				return 1;
			if (history_search_backward(history, &c->search_pos, c->search_text)) {
				set_text(c, history->ptrs[c->search_pos]);
			} else {
				set_text(c, c->search_text);
				c->search_pos = -1;
			}
			return 1;
		default:
			return 0;
		}
		break;
	case KEY_PASTE:
		cmdline_insert_paste(c);
		break;
	}
	c->search_pos = -1;
	return 1;
}
