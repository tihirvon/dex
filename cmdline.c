#include "cmdline.h"
#include "buffer.h"
#include "gbuf.h"
#include "term.h"

GBUF(cmdline);
int cmdline_pos;

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
		int i = cmdline_pos;
		uchar u = u_get_char(cmdline.buffer, &i);
		len = u_char_size(u);
	}
	gbuf_remove(&cmdline, cmdline_pos, len);
}

void cmdline_backspace(void)
{
	if (cmdline_pos) {
		u_prev_char_pos(cmdline.buffer, &cmdline_pos);
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
		u_prev_char_pos(cmdline.buffer, &cmdline_pos);
}

void cmdline_next_char(void)
{
	if (cmdline_pos < cmdline.len)
		u_get_char(cmdline.buffer, &cmdline_pos);
}

void cmdline_clear(void)
{
	gbuf_clear(&cmdline);
	cmdline_pos = 0;
}

