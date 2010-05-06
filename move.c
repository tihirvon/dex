#include "move.h"
#include "buffer.h"
#include "indent.h"

void move_to_preferred_x(void)
{
	unsigned int tw = buffer->options.tab_width;
	int in_space_indent = 1;
	int x = 0;
	uchar u;

	block_iter_bol(&view->cursor);
	while (x < view->preferred_x) {
		if (!buffer_next_char(&view->cursor, &u))
			break;

		if (u == ' ') {
			x++;
			continue;
		}

		in_space_indent = 0;
		if (u == '\t') {
			x = (x + tw) / tw * tw;
			if (x > view->preferred_x) {
				block_iter_prev_byte(&view->cursor, &u);
				break;
			}
		} else if (u == '\n') {
			block_iter_prev_byte(&view->cursor, &u);
			break;
		} else if (u < 0x20) {
			x += 2;
		} else {
			x++;
		}
	}

	if (buffer->options.emulate_tab && in_space_indent && x % buffer->options.indent_width) {
		// force cursor to beginning of a indentation level
		int count = x % buffer->options.indent_width;
		while (count--)
			block_iter_prev_byte(&view->cursor, &u);
	}
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
			block_iter_skip_bytes(&view->cursor, size);
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
	buffer_bof(&view->cursor);
	view->preferred_x = 0;
}

void move_eof(void)
{
	buffer_eof(&view->cursor);
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

unsigned int word_fwd(struct block_iter *bi)
{
	enum { space, word, other } type = other;
	unsigned int count;
	uchar u;

	if (!block_iter_next_byte(bi, &u))
		return 0;

	if (isspace(u))
		type = space;
	else if (is_word_byte(u))
		type = word;

	count = 1;
	while (block_iter_next_byte(bi, &u)) {
		count++;
		switch (type) {
		case space:
			if (isspace(u))
				continue;
			break;
		case word:
			if (is_word_byte(u))
				continue;
			break;
		case other:
			if (!isspace(u) && !is_word_byte(u))
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

	if (isspace(u))
		type = space;
	else if (is_word_byte(u))
		type = word;

	count = 1;
	while (block_iter_prev_byte(bi, &u)) {
		count++;
		switch (type) {
		case space:
			if (isspace(u))
				continue;
			break;
		case word:
			if (is_word_byte(u))
				continue;
			break;
		case other:
			if (!isspace(u) && !is_word_byte(u))
				continue;
			break;
		}
		block_iter_next_byte(bi, &u);
		count--;
		break;
	}
	return count;
}
