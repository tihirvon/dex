#include "cmdline.h"
#include "gbuf.h"
#include "common.h"
#include "uchar.h"

GBUF(cmdline);
unsigned int cmdline_pos;

void cmdline_insert(unsigned int u)
{
	unsigned int len = 1;

	if (term_utf8)
		len = u_char_size(u);
	gbuf_make_space(&cmdline, cmdline_pos, len);
	if (len > 1) {
		u_set_char_raw(cmdline.buffer, &cmdline_pos, u);
	} else {
		cmdline.buffer[cmdline_pos++] = u;
	}
}

void cmdline_delete(void)
{
	int len = 1;

	if (cmdline_pos == cmdline.len)
		return;

	if (term_utf8) {
		unsigned int pos = cmdline_pos;
		u_buf_get_char(cmdline.buffer, cmdline.len, &pos);
		len = pos - cmdline_pos;
	}
	gbuf_remove(&cmdline, cmdline_pos, len);
}

void cmdline_backspace(void)
{
	if (cmdline_pos) {
		u_prev_char(cmdline.buffer, &cmdline_pos);
		cmdline_delete();
	}
}

void cmdline_erase_word(void)
{
	int i = cmdline_pos;

	if (!cmdline_pos)
		return;

	// open /path/to/file^W => open /path/to/

	// erase whitespace
	while (i && isspace(cmdline.buffer[i - 1]))
		i--;

	// erase non-word bytes
	while (i && !is_word_byte(cmdline.buffer[i - 1]))
		i--;

	// erase word bytes
	while (i && is_word_byte(cmdline.buffer[i - 1]))
		i--;

	gbuf_remove(&cmdline, i, cmdline_pos - i);
	cmdline_pos = i;
}

void cmdline_delete_bol(void)
{
	gbuf_remove(&cmdline, 0, cmdline_pos);
	cmdline_pos = 0;
}

void cmdline_delete_eol(void)
{
	cmdline.buffer[cmdline_pos] = 0;
	cmdline.len = cmdline_pos;
}

void cmdline_prev_char(void)
{
	if (cmdline_pos)
		u_prev_char(cmdline.buffer, &cmdline_pos);
}

void cmdline_next_char(void)
{
	if (cmdline_pos < cmdline.len)
		u_buf_get_char(cmdline.buffer, cmdline.len, &cmdline_pos);
}

void cmdline_clear(void)
{
	gbuf_clear(&cmdline);
	cmdline_pos = 0;
}

void cmdline_set_text(const char *text)
{
	cmdline_clear();
	gbuf_add_str(&cmdline, text);
	cmdline_pos = strlen(text);
}

void cmdline_insert_bytes(const char *buf, int size)
{
	int i;

	gbuf_make_space(&cmdline, cmdline_pos, size);
	for (i = 0; i < size; i++)
		cmdline.buffer[cmdline_pos++] = buf[i];
}
