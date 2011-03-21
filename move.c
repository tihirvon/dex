#include "move.h"
#include "buffer.h"
#include "indent.h"
#include "uchar.h"

enum char_type {
	CT_SPACE,
	CT_WORD,
	CT_OTHER,
};

void move_to_preferred_x(void)
{
	unsigned int tw = buffer->options.tab_width;
	struct lineref lr;
	unsigned int i = 0;
	unsigned int x = 0;

	block_iter_bol(&view->cursor);
	fill_line_ref(&view->cursor, &lr);

	if (buffer->options.emulate_tab && view->preferred_x < lr.size) {
		while (i < view->preferred_x && lr.line[i] == ' ')
			i++;

		x = i;
		if (i == view->preferred_x) {
			// force cursor to beginning of the indentation level
			int iw = buffer->options.indent_width;
			view->cursor.offset += i / iw * iw;
			return;
		}
	}

	while (x < view->preferred_x && i < lr.size) {
		unsigned int u = lr.line[i++];

		if (u < 0x80) {
			if (!u_is_ctrl(u)) {
				x++;
			} else if (u == '\t') {
				x = (x + tw) / tw * tw;
			} else if (u == '\n') {
				break;
			} else {
				x += 2;
			}
		} else if (buffer->options.utf8) {
			int next = i;
			i--;
			u = u_buf_get_char(lr.line, lr.size, &i);
			x += u_char_width(u);
			if (x > view->preferred_x) {
				i = next;
				break;
			}
		} else if (u > 0x9f) {
			x++;
		} else {
			x += 4;
		}
	}
	if (x > view->preferred_x)
		i--;
	view->cursor.offset += i;
}

void move_cursor_left(void)
{
	unsigned int u;

	if (buffer->options.emulate_tab) {
		int size = get_indent_level_bytes_left();
		if (size) {
			block_iter_back_bytes(&view->cursor, size);
			update_preferred_x();
			return;
		}
	}

	buffer_prev_char(&view->cursor, &u);
	update_preferred_x();
}

void move_cursor_right(void)
{
	unsigned int u;

	if (buffer->options.emulate_tab) {
		int size = get_indent_level_bytes_right();
		if (size) {
			block_iter_skip_bytes(&view->cursor, size);
			update_preferred_x();
			return;
		}
	}

	buffer_next_char(&view->cursor, &u);
	update_preferred_x();
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
		if (!block_iter_eat_line(&view->cursor))
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
	block_iter_goto_line(&view->cursor, line - 1);
	view->center_on_scroll = 1;
}

void move_to_column(int column)
{
	block_iter_bol(&view->cursor);
	while (column-- > 1) {
		unsigned int u;

		if (!buffer_next_char(&view->cursor, &u))
			break;
		if (u == '\n') {
			block_iter_prev_byte(&view->cursor, &u);
			break;
		}
	}
	update_preferred_x();
}

static enum char_type get_char_type(char ch)
{
	if (isspace(ch))
		return CT_SPACE;
	if (is_word_byte(ch))
		return CT_WORD;
	return CT_OTHER;
}

static int get_current_char_type(struct block_iter *bi, enum char_type *type)
{
	unsigned int u;

	if (!buffer_get_char(bi, &u))
		return 0;

	*type = get_char_type(u);
	return 1;
}

static unsigned int skip_fwd_char_type(struct block_iter *bi, enum char_type type)
{
	unsigned int count = 0;
	unsigned int u;

	while (block_iter_next_byte(bi, &u)) {
		if (get_char_type(u) != type) {
			block_iter_prev_byte(bi, &u);
			return count;
		}
		count++;
	}
	return count;
}

static unsigned int skip_bwd_char_type(struct block_iter *bi, enum char_type type)
{
	unsigned int count = 0;
	unsigned int u;

	while (block_iter_prev_byte(bi, &u)) {
		if (get_char_type(u) != type) {
			block_iter_next_byte(bi, &u);
			return count;
		}
		count++;
	}
	return count;
}

unsigned int word_fwd(struct block_iter *bi, int skip_non_word)
{
	unsigned int count = 0;
	enum char_type type;

	while (1) {
		count += skip_fwd_char_type(bi, CT_SPACE);
		if (!get_current_char_type(bi, &type))
			return count;

		if (count && (!skip_non_word || type == CT_WORD))
			return count;

		count += skip_fwd_char_type(bi, type);
	}
}

unsigned int word_bwd(struct block_iter *bi, int skip_non_word)
{
	unsigned int count = 0;
	enum char_type type;
	unsigned int u;

	do {
		count += skip_bwd_char_type(bi, CT_SPACE);
		if (!block_iter_prev_byte(bi, &u))
			return count;

		type = get_char_type(u);
		count++;
		count += skip_bwd_char_type(bi, type);
	} while (skip_non_word && type != CT_WORD);
	return count;
}
