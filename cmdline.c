#include "cmdline.h"
#include "buffer.h"
#include "gbuf.h"
#include "term.h"

GBUF(cmdline);
unsigned int cmdline_pos;

void cmdline_insert(uchar u)
{
	int len = 1;

	if (term_flags & TERM_UTF8)
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

	if (term_flags & TERM_UTF8) {
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
	} else {
		if (!cmdline.len)
			input_mode = INPUT_NORMAL;
	}
}

void cmdline_delete_bol(void)
{
	gbuf_remove(&cmdline, 0, cmdline_pos);
	cmdline_pos = 0;
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
