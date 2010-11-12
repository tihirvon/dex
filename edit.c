#include "edit.h"
#include "move.h"
#include "buffer.h"
#include "change.h"
#include "block.h"
#include "gbuf.h"
#include "indent.h"

struct paragraph_formatter {
	struct gbuf buf;
	char *indent;
	int indent_len;
	int indent_width;
	int cur_width;
	int text_width;
};

static char *copy_buf;
static unsigned int copy_len;
static int copy_is_lines;

void insert(const char *buf, unsigned int len)
{
	record_insert(len);
	do_insert(buf, len);
}

void delete(unsigned int len, int move_after)
{
	if (len) {
		char *buf = do_delete(len);
		record_delete(buf, len, move_after);
		update_preferred_x();
	}
}

void replace(unsigned int del_count, const char *inserted, int ins_count)
{
	char *deleted = NULL;

	if (!del_count) {
		if (!ins_count)
			return;
		do_insert(inserted, ins_count);
	} else if (!ins_count) {
		deleted = do_delete(del_count);
	} else {
		deleted = do_replace(del_count, inserted, ins_count);
	}
	record_replace(deleted, del_count, ins_count);
}

void unselect(void)
{
	if (selecting()) {
		view->selection = SELECT_NONE;
		update_flags |= UPDATE_FULL;
	}
}

static void record_copy(char *buf, unsigned int len, int is_lines)
{
	if (copy_buf)
		free(copy_buf);
	copy_buf = buf;
	copy_len = len;
	copy_is_lines = is_lines;
}

void cut(unsigned int len, int is_lines)
{
	if (len) {
		char *buf = do_delete(len);
		record_copy(xmemdup(buf, len), len, is_lines);
		record_delete(buf, len, 0);
	}
}

void copy(unsigned int len, int is_lines)
{
	if (len) {
		char *buf = buffer_get_bytes(len);
		record_copy(buf, len, is_lines);
	}
}

unsigned int prepare_selection(void)
{
	struct selection_info info;
	init_selection(&info);
	view->cursor = info.si;
	return info.eo - info.so;
}

void paste(void)
{
	unsigned int del_count = 0;

	if (!copy_buf)
		return;

	if (selecting()) {
		del_count = prepare_selection();
		unselect();
	}

	if (copy_buf[copy_len - 1] != '\n') {
		struct block_iter bi = view->cursor;
		block_iter_skip_bytes(&bi, del_count);
		if (block_iter_is_eof(&bi)) {
			xrenew(copy_buf, copy_len + 1);
			copy_buf[copy_len++] = '\n';
		}
	}

	if (copy_is_lines) {
		if (!del_count)
			block_iter_eat_line(&view->cursor);
		replace(del_count, copy_buf, copy_len);
		move_to_preferred_x();
		update_preferred_x();
	} else {
		replace(del_count, copy_buf, copy_len);
	}
}

static void delete_one_ch(void)
{
	struct block_iter bi = view->cursor;
	uchar u;

	if (!buffer_next_char(&bi, &u))
		return;

	if (u == '\n' && block_iter_is_eof(&bi) &&
			!block_iter_is_bol(&view->cursor) &&
			!options.allow_incomplete_last_line) {
		/* don't make last line incomplete */
		return;
	}

	if (buffer->options.utf8) {
		delete(u_char_size(u), 0);
	} else {
		delete(1, 0);
	}
}

void delete_ch(void)
{
	if (selecting()) {
		unsigned int len;

		len = prepare_selection();
		delete(len, 0);
		unselect();
	} else {
		begin_change(CHANGE_MERGE_DELETE);

		if (buffer->options.emulate_tab) {
			int size = get_indent_level_bytes_right();
			if (size) {
				delete(size, 0);
				return;
			}
		}

		delete_one_ch();
	}
}

void erase(void)
{
	if (selecting()) {
		unsigned int len;

		len = prepare_selection();
		delete(len, 1);
		unselect();
	} else {
		uchar u;

		begin_change(CHANGE_MERGE_ERASE);

		if (buffer->options.emulate_tab) {
			int size = get_indent_level_bytes_left();
			if (size) {
				block_iter_back_bytes(&view->cursor, size);
				delete(size, 1);
				return;
			}
		}

		if (buffer_prev_char(&view->cursor, &u)) {
			if (buffer->options.utf8) {
				delete(u_char_size(u), 1);
			} else {
				delete(1, 1);
			}
		}
	}
}

// goto beginning of whitespace (tabs and spaces) under cursor and
// return number of whitespace bytes after cursor after moving cursor
static int goto_beginning_of_whitespace(void)
{
	struct block_iter bi = view->cursor;
	int count = 0;
	uchar u;

	// count spaces and tabs at or after cursor
	while (block_iter_next_byte(&bi, &u)) {
		if (u != '\t' && u != ' ')
			break;
		count++;
	}

	// count spaces and tabs before cursor
	while (block_iter_prev_byte(&view->cursor, &u)) {
		if (u != '\t' && u != ' ') {
			block_iter_next_byte(&view->cursor, &u);
			break;
		}
		count++;
	}
	return count;
}

void insert_ch(unsigned int ch)
{
	if (selecting())
		delete_ch();

	if (ch == '\n') {
		char *indent = NULL;
		char *deleted = NULL;
		int ins_count = 0;
		int del_count = 0;

		if (buffer->options.auto_indent) {
			indent = get_indent();
			if (indent)
				ins_count += strlen(indent);
		}
		if (buffer->options.trim_whitespace) {
			del_count = goto_beginning_of_whitespace();
			if (del_count)
				deleted = do_delete(del_count);
		}
		if (indent) {
			do_insert(indent, ins_count);
			free(indent);
		}
		do_insert("\n", 1);
		ins_count++;

		begin_change(CHANGE_MERGE_NONE);
		record_replace(deleted, del_count, ins_count);
		end_change();

		block_iter_skip_bytes(&view->cursor, ins_count);
		update_preferred_x();
	} else {
		char buf[9];
		unsigned int bytes, i = 0;

		if (ch == '\t' && buffer->options.expand_tab) {
			i = buffer->options.indent_width;
			memset(buf, ' ', i);
		} else if (buffer->options.utf8) {
			u_set_char_raw(buf, &i, ch);
		} else {
			buf[i++] = ch;
		}
		bytes = i;
		if (block_iter_is_eof(&view->cursor))
			buf[i++] = '\n';

		begin_change(CHANGE_MERGE_INSERT);
		insert(buf, i);
		end_change();

		block_iter_skip_bytes(&view->cursor, bytes);
		update_preferred_x();
	}
}

static void join_selection(void)
{
	unsigned int count = prepare_selection();
	unsigned int len = 0, join = 0;
	struct block_iter bi;
	uchar ch = 0;

	unselect();
	bi = view->cursor;

	begin_change_chain();
	while (count) {
		if (!len)
			view->cursor = bi;

		block_iter_next_byte(&bi, &ch);
		if (ch == '\t' || ch == ' ') {
			len++;
		} else if (ch == '\n') {
			len++;
			join++;
		} else {
			if (join) {
				replace(len, " ", 1);
				/* skip the space we inserted and the char we read last */
				block_iter_next_byte(&view->cursor, &ch);
				block_iter_next_byte(&view->cursor, &ch);
				bi = view->cursor;
			}
			len = 0;
			join = 0;
		}
		count--;
	}

	/* don't replace last \n which is at end of the selection */
	if (join && ch == '\n') {
		join--;
		len--;
	}

	if (join) {
		if (ch == '\n') {
			/* don't add space to end of line */
			delete(len, 0);
		} else {
			replace(len, " ", 1);
		}
	}
	end_change_chain();
	update_preferred_x();
}

void join_lines(void)
{
	struct block_iter next, bi = view->cursor;
	int count;
	uchar u;
	char *buf;

	if (selecting()) {
		join_selection();
		return;
	}

	if (!block_iter_next_line(&bi))
		return;
	if (block_iter_is_eof(&bi))
		return;

	next = bi;
	block_iter_prev_byte(&bi, &u);
	count = 1;
	while (block_iter_prev_byte(&bi, &u)) {
		if (u != '\t' && u != ' ') {
			block_iter_next_byte(&bi, &u);
			break;
		}
		count++;
	}
	while (block_iter_next_byte(&next, &u)) {
		if (u != '\t' && u != ' ')
			break;
		count++;
	}

	view->cursor = bi;
	buf = do_delete(count);
	do_insert(" ", 1);
	record_replace(buf, count, 1);
	update_preferred_x();
}

char *get_word_under_cursor(void)
{
	struct lineref lr;
	unsigned int ei, si = fetch_this_line(&view->cursor, &lr);

	while (si < lr.size && !is_word_byte(lr.line[si]))
		si++;

	if (si == lr.size)
		return NULL;

	ei = si;
	while (si > 0 && is_word_byte(lr.line[si - 1]))
		si--;
	while (ei + 1 < lr.size && is_word_byte(lr.line[ei + 1]))
		ei++;
	return xstrndup(lr.line + si, ei - si + 1);
}

static void shift_right(int nr_lines, int count)
{
	int i, indent_size;
	char *indent;

	indent = alloc_indent(count, &indent_size);
	i = 0;
	while (1) {
		struct indent_info info;
		struct lineref lr;

		fetch_this_line(&view->cursor, &lr);
		get_indent_info(lr.line, lr.size, &info);
		if (info.wsonly) {
			if (info.bytes) {
				// remove indentation
				char *deleted;

				deleted = do_delete(info.bytes);
				record_delete(deleted, info.bytes, 0);
			}
		} else if (info.sane) {
			// insert whitespace
			do_insert(indent, indent_size);
			record_insert(indent_size);
		} else {
			// replace whole indentation with sane one
			int level = info.level + count;
			char *deleted;
			char *buf;
			int size;

			deleted = do_delete(info.bytes);
			buf = alloc_indent(level, &size);
			do_insert(buf, size);
			free(buf);
			record_replace(deleted, info.bytes, size);
		}
		if (++i == nr_lines)
			break;
		block_iter_eat_line(&view->cursor);
	}
	free(indent);
}

static void shift_left(int nr_lines, int count)
{
	int i;

	i = 0;
	while (1) {
		struct indent_info info;
		struct lineref lr;

		fetch_this_line(&view->cursor, &lr);
		get_indent_info(lr.line, lr.size, &info);
		if (info.wsonly) {
			if (info.bytes) {
				// remove indentation
				char *deleted;

				deleted = do_delete(info.bytes);
				record_delete(deleted, info.bytes, 0);
			}
		} else if (info.level && info.sane) {
			char *buf;
			int n = count;

			if (n > info.level)
				n = info.level;
			if (use_spaces_for_indent())
				n *= buffer->options.indent_width;
			buf = do_delete(n);
			record_delete(buf, n, 0);
		} else if (info.bytes) {
			// replace whole indentation with sane one
			char *deleted;

			deleted = do_delete(info.bytes);
			if (info.level > count) {
				char *buf;
				int size;

				buf = alloc_indent(info.level - count, &size);
				do_insert(buf, size);
				free(buf);
				record_replace(deleted, info.bytes, size);
			} else {
				record_delete(deleted, info.bytes, 0);
			}
		}
		if (++i == nr_lines)
			break;
		block_iter_eat_line(&view->cursor);
	}
}

void shift_lines(int count)
{
	int nr_lines = 1;
	struct selection_info info;

	if (selecting()) {
		view->selection = SELECT_LINES;
		fill_selection_info(&info);
		view->cursor = info.si;
		nr_lines = info.nr_lines;
	}

	view->preferred_x += buffer->options.indent_width * count;
	if (view->preferred_x < 0)
		view->preferred_x = 0;

	begin_change_chain();
	block_iter_bol(&view->cursor);
	if (count > 0)
		shift_right(nr_lines, count);
	else
		shift_left(nr_lines, -count);
	end_change_chain();

	if (selecting()) {
		if (info.swapped) {
			// cursor should be at beginning of selection
			block_iter_bol(&view->cursor);
			view->sel_so = block_iter_get_offset(&view->cursor);
			while (--nr_lines)
				block_iter_prev_line(&view->cursor);
		} else {
			struct block_iter save = view->cursor;
			while (--nr_lines)
				block_iter_prev_line(&view->cursor);
			view->sel_so = block_iter_get_offset(&view->cursor);
			view->cursor = save;
		}
	}
	move_to_preferred_x();
}

void clear_lines(void)
{
	unsigned int del_count = 0, ins_count = 0;
	char *indent = get_indent();

	if (selecting()) {
		view->selection = SELECT_LINES;
		del_count = prepare_selection();
		unselect();

		// don't delete last newline
		if (del_count)
			del_count--;
	} else {
		block_iter_eol(&view->cursor);
		del_count = block_iter_bol(&view->cursor);
	}

	if (!indent && !del_count)
		return;

	if (indent)
		ins_count = strlen(indent);
	replace(del_count, indent, ins_count);
	block_iter_skip_bytes(&view->cursor, ins_count);
	update_preferred_x();
}

void new_line(void)
{
	unsigned int ins_count = 1;

	block_iter_eol(&view->cursor);

	if (buffer->options.auto_indent) {
		char *indent = get_indent();
		if (indent) {
			ins_count += strlen(indent);
			do_insert(indent, ins_count - 1);
			free(indent);
		}
	}
	do_insert("\n", 1);

	record_insert(ins_count);
	block_iter_skip_bytes(&view->cursor, ins_count);
	update_preferred_x();
}

static void add_word(struct paragraph_formatter *pf, const char *word, int len)
{
	unsigned int i = 0;
	int word_width = 0;
	int bol = !pf->cur_width;

	while (i < len) {
		unsigned char ch = word[i];
		if (ch < 0x80 || !buffer->options.utf8) {
			word_width++;
			i++;
		} else {
			word_width += u_char_width(u_buf_get_char(word, len, &i));
		}
	}

	if (pf->cur_width + 1 + word_width > pf->text_width) {
		gbuf_add_ch(&pf->buf, '\n');
		pf->cur_width = 0;
		bol = 1;
	}

	if (bol) {
		gbuf_add_buf(&pf->buf, pf->indent, pf->indent_len);
		pf->cur_width = pf->indent_width;
	} else {
		gbuf_add_ch(&pf->buf, ' ');
		pf->cur_width++;
	}

	gbuf_add_buf(&pf->buf, word, len);
	pf->cur_width += word_width;
}

static unsigned int paragraph_size(void)
{
	struct block_iter bi = view->cursor;
	struct lineref lr;
	struct indent_info ii;
	struct indent_info info;
	unsigned int size;

	block_iter_bol(&bi);
	fill_line_ref(&bi, &lr);
	get_indent_info(lr.line, lr.size, &info);
	if (info.wsonly) {
		// not in paragraph
		return 0;
	}

	// goto beginning of paragraph
	while (block_iter_prev_line(&bi)) {
		fill_line_ref(&bi, &lr);
		get_indent_info(lr.line, lr.size, &ii);
		if (ii.wsonly || ii.width != info.width) {
			// empty line or indent changed
			block_iter_eat_line(&bi);
			break;
		}
	}
	view->cursor = bi;

	// get size of paragraph
	size = 0;
	do {
		unsigned int bytes = block_iter_eat_line(&bi);

		if (!bytes)
			break;

		size += bytes;
		fill_line_ref(&bi, &lr);
		get_indent_info(lr.line, lr.size, &ii);
	} while (!ii.wsonly && ii.width == info.width);
	return size;
}

void format_paragraph(int text_width)
{
	struct paragraph_formatter pf;
	struct indent_info info;
	unsigned int len, i;
	char *sel;

	if (selecting()) {
		view->selection = SELECT_LINES;
		len = prepare_selection();
	} else {
		len = paragraph_size();
	}
	if (!len)
		return;

	sel = do_delete(len);
	get_indent_info(sel, len, &info);

	gbuf_init(&pf.buf);
	pf.indent = make_indent(&info);
	pf.indent_len = pf.indent ? strlen(pf.indent) : 0;
	pf.indent_width = info.width;
	pf.cur_width = 0;
	pf.text_width = text_width;

	i = 0;
	while (1) {
		unsigned int start;

		while (i < len && isspace(sel[i]))
			i++;
		if (i == len)
			break;

		start = i;
		while (i < len && !isspace(sel[i]))
			i++;

		add_word(&pf, sel + start, i - start);
	}

	if (pf.buf.len) {
		gbuf_add_ch(&pf.buf, '\n');
		do_insert(pf.buf.buffer, pf.buf.len);
	}
	record_replace(sel, len, pf.buf.len);
	if (pf.buf.len)
		block_iter_skip_bytes(&view->cursor, pf.buf.len - 1);
	gbuf_free(&pf.buf);
	free(pf.indent);

	unselect();
	update_preferred_x();
}

void change_case(int mode, int move_after)
{
	unsigned int text_len, i;
	char *src;
	GBUF(dst);

	if (selecting()) {
		text_len = prepare_selection();
		unselect();
	} else {
		uchar u;

		if (!buffer_get_char(&view->cursor, &u))
			return;

		text_len = 1;
		if (buffer->options.utf8)
			text_len = u_char_size(u);
	}

	src = do_delete(text_len);
	i = 0;
	while (i < text_len) {
		uchar u;

		if (buffer->options.utf8)
			u = u_buf_get_char(src, text_len, &i);
		else
			u = src[i++];

		// this works with latin1 too
		switch (mode) {
		case 't':
			if (iswupper(u))
				u = towlower(u);
			else
				u = towupper(u);
			break;
		case 'l':
			u = towlower(u);
			break;
		case 'u':
			u = towupper(u);
			break;
		}

		if (buffer->options.utf8) {
			char buf[4];
			unsigned int idx = 0;
			u_set_char_raw(buf, &idx, u);
			gbuf_add_buf(&dst, buf, idx);
		} else {
			gbuf_add_ch(&dst, u);
		}
	}

	do_insert(dst.buffer, dst.len);
	record_replace(src, text_len, dst.len);

	if (move_after)
		block_iter_skip_bytes(&view->cursor, dst.len);

	gbuf_free(&dst);
}
