#include "move.h"
#include "buffer.h"

static int get_current_indent_bytes(const char *buf, int cursor_offset)
{
	int tw = buffer->options.tab_width;
	int ibytes = 0;
	int iwidth = 0;
	int i;

	for (i = 0; i < cursor_offset; i++) {
		char ch = buf[i];

		if (iwidth % buffer->options.indent_width == 0) {
			ibytes = 0;
			iwidth = 0;
		}

		if (ch == '\t') {
			iwidth = (iwidth + tw) / tw * tw;
		} else if (ch == ' ') {
			iwidth++;
		} else {
			// cursor not at indentation
			return -1;
		}
		ibytes++;
	}

	if (iwidth % buffer->options.indent_width) {
		// cursor at middle of indentation level
		return -1;
	}
	return ibytes;
}

int get_indent_level_bytes_left(void)
{
	struct lineref lr;
	unsigned int cursor_offset = fetch_this_line(&view->cursor, &lr);
	int ibytes;

	if (!cursor_offset)
		return 0;

	ibytes = get_current_indent_bytes(lr.line, cursor_offset);
	if (ibytes < 0)
		return 0;
	return ibytes;
}

int get_indent_level_bytes_right(void)
{
	struct lineref lr;
	unsigned int cursor_offset = fetch_this_line(&view->cursor, &lr);
	int tw = buffer->options.tab_width;
	int i, ibytes, iwidth;

	ibytes = get_current_indent_bytes(lr.line, cursor_offset);
	if (ibytes < 0)
		return 0;

	iwidth = 0;
	for (i = cursor_offset; i < lr.size; i++) {
		char ch = lr.line[i];

		if (ch == '\t') {
			iwidth = (iwidth + tw) / tw * tw;
		} else if (ch == ' ') {
			iwidth++;
		} else {
			// no full indentation level at cursor position
			return 0;
		}

		if (iwidth % buffer->options.indent_width == 0)
			return i - cursor_offset + 1;
	}
	return 0;
}

void move_left(int count)
{
	while (count > 0) {
		uchar u;

		if (!buffer_prev_char(&view->cursor, &u))
			break;
		if (!options.move_wraps && u == '\n') {
			block_iter_next_byte(&view->cursor, &u);
			break;
		}
		count--;
	}
	update_preferred_x();
}

void move_right(int count)
{
	while (count > 0) {
		uchar u;

		if (!buffer_next_char(&view->cursor, &u))
			break;
		if (!options.move_wraps && u == '\n') {
			block_iter_prev_byte(&view->cursor, &u);
			break;
		}
		count--;
	}
	update_preferred_x();
}

void move_cursor_left(void)
{
	if (buffer->options.emulate_tab) {
		int size = get_indent_level_bytes_left();
		if (size) {
			uchar u;
			while (size--)
				block_iter_prev_byte(&view->cursor, &u);
			update_preferred_x();
			return;
		}
	}
	move_left(1);
}

void move_cursor_right(void)
{
	if (buffer->options.emulate_tab) {
		int size = get_indent_level_bytes_right();
		if (size) {
			uchar u;
			while (size--)
				block_iter_next_byte(&view->cursor, &u);
			update_preferred_x();
			return;
		}
	}
	move_right(1);
}

void move_bol(void)
{
	block_iter_bol(&view->cursor);
	update_preferred_x();
}

void move_eol(void)
{
	block_iter_eol(&view->cursor);
	update_preferred_x();
}

void move_up(int count)
{
	while (count > 0) {
		if (!block_iter_prev_line(&view->cursor))
			break;
		count--;
	}
	move_to_preferred_x();
}

void move_down(int count)
{
	while (count > 0) {
		if (!block_iter_next_line(&view->cursor))
			break;
		count--;
	}
	move_to_preferred_x();
}

void move_bof(void)
{
	view->cursor.blk = BLOCK(buffer->blocks.next);
	view->cursor.offset = 0;
	view->preferred_x = 0;
}

void move_eof(void)
{
	view->cursor.blk = BLOCK(buffer->blocks.prev);
	view->cursor.offset = view->cursor.blk->size;
	update_preferred_x();
}

void move_to_line(int line)
{
	struct block *blk;
	unsigned int nl = 0;

	line--;
	list_for_each_entry(blk, &buffer->blocks, node) {
		if (nl + blk->nl > line)
			break;
		nl += blk->nl;
	}

	view->cursor.blk = blk;
	view->cursor.offset = 0;
	while (nl < line) {
		if (!block_iter_next_line(&view->cursor))
			break;
		nl++;
	}
	view->center_on_scroll = 1;
}

void move_to_column(int column)
{
	view->preferred_x = column - 1;
	move_to_preferred_x();
}

static int is_whitespace(unsigned char byte)
{
	return byte == ' ' || byte == '\t' || byte == '\n';
}

unsigned int word_fwd(struct block_iter *bi)
{
	enum { space, word, other } type = other;
	unsigned int count;
	uchar u;

	if (!block_iter_next_byte(bi, &u))
		return 0;

	if (is_whitespace(u))
		type = space;
	else if (is_word_byte(u))
		type = word;

	count = 1;
	while (block_iter_next_byte(bi, &u)) {
		count++;
		switch (type) {
		case space:
			if (is_whitespace(u))
				continue;
			break;
		case word:
			if (is_word_byte(u))
				continue;
			break;
		case other:
			if (!is_whitespace(u) && !is_word_byte(u))
				continue;
			break;
		}
		block_iter_prev_byte(bi, &u);
		count--;
		break;
	}
	return count;
}

unsigned int word_bwd(struct block_iter *bi)
{
	enum { space, word, other } type = other;
	unsigned int count;
	uchar u;

	if (!block_iter_prev_byte(bi, &u))
		return 0;

	if (is_whitespace(u))
		type = space;
	else if (is_word_byte(u))
		type = word;

	count = 1;
	while (block_iter_prev_byte(bi, &u)) {
		count++;
		switch (type) {
		case space:
			if (is_whitespace(u))
				continue;
			break;
		case word:
			if (is_word_byte(u))
				continue;
			break;
		case other:
			if (!is_whitespace(u) && !is_word_byte(u))
				continue;
			break;
		}
		block_iter_next_byte(bi, &u);
		count--;
		break;
	}
	return count;
}
