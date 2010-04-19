#include "edit.h"
#include "move.h"
#include "buffer.h"
#include "buffer-highlight.h"
#include "change.h"
#include "block.h"
#include "gbuf.h"

unsigned int update_flags;

static char *copy_buf;
static unsigned int copy_len;
static int copy_is_lines;

void insert(const char *buf, unsigned int len)
{
	record_insert(len);
	do_insert(buf, len);
	update_preferred_x();
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

	if (del_count)
		deleted = do_delete(del_count);
	if (ins_count)
		do_insert(inserted, ins_count);
	if (del_count || ins_count) {
		record_replace(deleted, del_count, ins_count);
		update_preferred_x();
	}
}

void select_end(void)
{
	if (view->sel.blk) {
		view->sel.blk = NULL;
		view->sel.offset = 0;
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

unsigned int count_bytes_eol(struct block_iter *bi)
{
	unsigned int count = 0;
	uchar u;

	do {
		if (!block_iter_next_byte(bi, &u))
			break;
		count++;
	} while (u != '\n');
	return count;
}

unsigned int prepare_selection(void)
{
	struct selection_info info;
	init_selection(&info);
	view->cursor = info.si;
	view->sel = info.ei;
	return info.eo - info.so;
}

void paste(void)
{
	if (selecting())
		delete_ch();

	undo_merge = UNDO_MERGE_NONE;
	if (!copy_buf)
		return;
	if (copy_is_lines) {
		update_preferred_x();
		block_iter_next_line(&view->cursor);

		record_insert(copy_len);
		do_insert(copy_buf, copy_len);

		move_to_preferred_x();
	} else {
		insert(copy_buf, copy_len);
	}
}

static int would_become_empty(void)
{
	struct block *blk;
	int size = 0;

	list_for_each_entry(blk, &buffer->blocks, node) {
		size += blk->size;
		if (size > 1)
			return 0;
	}
	return 1;
}

static void delete_one_ch(void)
{
	struct block_iter bi = view->cursor;
	uchar u;

	if (!buffer_next_char(&bi, &u))
		return;

	if (u == '\n' && !options.allow_incomplete_last_line &&
			block_iter_eof(&bi) && !would_become_empty()) {
		/* don't make last line incomplete */
	} else if (buffer->utf8) {
		delete(u_char_size(u), 0);
	} else {
		delete(1, 0);
	}
}

void delete_ch(void)
{
	if (selecting()) {
		unsigned int len;

		undo_merge = UNDO_MERGE_NONE;
		len = prepare_selection();
		delete(len, 0);
		select_end();
	} else {
		if (undo_merge != UNDO_MERGE_DELETE)
			undo_merge = UNDO_MERGE_NONE;

		if (buffer->options.emulate_tab) {
			int size = get_indent_level_bytes_right();
			if (size) {
				delete(size, 0);
				undo_merge = UNDO_MERGE_DELETE;
				return;
			}
		}

		delete_one_ch();
		undo_merge = UNDO_MERGE_DELETE;
	}
}

void erase(void)
{
	if (selecting()) {
		unsigned int len;

		undo_merge = UNDO_MERGE_NONE;
		len = prepare_selection();
		delete(len, 1);
		select_end();
	} else {
		uchar u;

		if (undo_merge != UNDO_MERGE_BACKSPACE)
			undo_merge = UNDO_MERGE_NONE;

		if (buffer->options.emulate_tab) {
			int size = get_indent_level_bytes_left();
			if (size) {
				move_left(size);
				delete(size, 1);
				undo_merge = UNDO_MERGE_BACKSPACE;
				return;
			}
		}

		if (buffer_prev_char(&view->cursor, &u)) {
			if (buffer->utf8) {
				delete(u_char_size(u), 1);
			} else {
				delete(1, 1);
			}
		}
		undo_merge = UNDO_MERGE_BACKSPACE;
	}
}

// get indentation of current or previous non-whitespace-only line
static char *get_indent(void)
{
	struct block_iter bi = view->cursor;

	block_iter_bol(&bi);
	do {
		struct lineref lr;
		int i;

		fill_line_ref(&bi, &lr);
		for (i = 0; i < lr.size; i++) {
			char ch = lr.line[i];

			if (ch != ' ' && ch != '\t') {
				char *str;

				if (!i)
					return NULL;
				str = xmemdup(lr.line, i + 1);
				str[i] = 0;
				return str;
			}
		}
	} while (block_iter_prev_line(&bi));
	return NULL;
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

	if (undo_merge != UNDO_MERGE_INSERT)
		undo_merge = UNDO_MERGE_NONE;

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
		record_replace(deleted, del_count, ins_count);
		move_right(ins_count);
		undo_merge = UNDO_MERGE_NONE;
	} else {
		char buf[9];
		unsigned int chars = 1;
		unsigned int i = 0;

		if (ch == '\t' && buffer->options.expand_tab) {
			i = chars = buffer->options.indent_width;
			memset(buf, ' ', chars);
		} else if (buffer->utf8) {
			u_set_char_raw(buf, &i, ch);
		} else {
			buf[i++] = ch;
		}
		if (block_iter_eof(&view->cursor))
			buf[i++] = '\n';
		insert(buf, i);
		move_right(chars);

		undo_merge = UNDO_MERGE_INSERT;
	}
}

static void join_selection(void)
{
	unsigned int count = prepare_selection();
	unsigned int len = 0, join = 0, del = 0;
	struct block_iter bi;
	uchar ch = 0;

	select_end();
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
				del += len - 1;
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
			del += len;
		} else {
			replace(len, " ", 1);
			del += len - 1;
		}
	}
	end_change_chain();
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
	if (block_iter_eof(&bi))
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

	undo_merge = UNDO_MERGE_NONE;
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

static int use_spaces_for_indent(void)
{
	return buffer->options.expand_tab || buffer->options.indent_width != buffer->options.tab_width;
}

static char *alloc_indent(int count, int *sizep)
{
	char *indent;
	int size;

	if (use_spaces_for_indent()) {
		size = buffer->options.indent_width * count;
		indent = xnew(char, size);
		memset(indent, ' ', size);
	} else {
		size = count;
		indent = xnew(char, size);
		memset(indent, '\t', size);
	}
	*sizep = size;
	return indent;
}

struct indent_info {
	int tabs;
	int spaces;
	int bytes;
	int width;
	int level;
	int sane;
	int wsonly;
};

static void get_indent_info(const char *buf, int len, struct indent_info *info)
{
	int pos = 0;

	memset(info, 0, sizeof(struct indent_info));
	info->sane = 1;
	while (pos < len) {
		if (buf[pos] == ' ') {
			info->width++;
			info->spaces++;
		} else if (buf[pos] == '\t') {
			int tw = buffer->options.tab_width;
			info->width = (info->width + tw) / tw * tw;
			info->tabs++;
		} else {
			break;
		}
		info->bytes++;
		pos++;

		if (info->width % buffer->options.indent_width == 0 && info->sane)
			info->sane = use_spaces_for_indent() ? !info->tabs : !info->spaces;
	}
	info->level = info->width / buffer->options.indent_width;
	info->wsonly = pos == len;
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
		block_iter_next_line(&view->cursor);
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
		block_iter_next_line(&view->cursor);
	}
}

void shift_lines(int count)
{
	int nr_lines = 1;
	struct selection_info info;

	if (selecting()) {
		view->selection = SELECT_LINES;
		init_selection(&info);
		fill_selection_info(&info);
		view->cursor = info.si;
		view->sel = info.ei;
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

	// only the cursor line is automatically updated
	if (nr_lines > 1)
		update_flags |= UPDATE_FULL;

	if (selecting()) {
		// make sure sel points to valid block
		block_iter_goto_offset(&view->sel, info.so);

		// restore cursor position as well as possible
		if (!info.swapped) {
			struct block_iter tmp = view->sel;
			view->sel = view->cursor;
			view->cursor = tmp;
		}
	}
	move_to_preferred_x();
}

void clear_lines(void)
{
	unsigned int del_count, ins_count = 0;
	char *indent, *deleted;

	indent = get_indent();

	block_iter_eol(&view->cursor);
	del_count = block_iter_bol(&view->cursor);
	if (!indent && !del_count)
		return;

	deleted = do_delete(del_count);
	if (indent) {
		ins_count = strlen(indent);
		do_insert(indent, ins_count);
		free(indent);
	}
	record_replace(deleted, del_count, ins_count);
	move_right(ins_count);

	undo_merge = UNDO_MERGE_NONE;
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
	move_right(ins_count);

	undo_merge = UNDO_MERGE_NONE;
}

static void add_paragraph_line(struct gbuf *buf, const char *str, int len)
{
	int i = 0;
	int dot = 0;

	while (i < len) {
		if (isspace(str[i])) {
			i++;
			while (i < len && isspace(str[i]))
				i++;
			if (i == len)
				break;
			gbuf_add_ch(buf, ' ');
			if (dot)
				gbuf_add_ch(buf, ' ');
		} else {
			char ch = str[i++];
			gbuf_add_ch(buf, ch);
			dot = ch == '.' || ch == '?' || ch == '!';
		}
	}
	gbuf_add_ch(buf, '\n');
}

static int is_ws_line(const struct block_iter *i)
{
	struct block_iter bi = *i;
	uchar ch;

	while (block_iter_next_byte(&bi, &ch)) {
		if (ch == '\n')
			return 1;
		if (!isspace(ch))
			return 0;
	}
	return 1;
}

/*
 * Goto beginning of current paragraph or beginning of next paragraph
 * if not currently on a paragraph.
 */
static void goto_bop(void)
{
	int in_paragraph = 1;

	block_iter_bol(&view->cursor);
	while (is_ws_line(&view->cursor)) {
		in_paragraph = 0;
		if (!block_iter_next_line(&view->cursor))
			break;
	}
	while (in_paragraph && block_iter_prev_line(&view->cursor)) {
		if (is_ws_line(&view->cursor)) {
			block_iter_next_line(&view->cursor);
			break;
		}
	}
}

static unsigned int goto_eop(struct block_iter *bi)
{
	unsigned int count = 0;

	while (1) {
		unsigned int c;

		if (is_ws_line(bi))
			break;
		c = block_iter_next_line(bi);
		if (!c) {
			count += block_iter_eol(bi);
			break;
		}
		count += c;
	}
	return count;
}

void format_paragraph(int text_width)
{
	struct indent_info info;
	unsigned int len, i;
	char *sel;
	GBUF(buf);

	undo_merge = UNDO_MERGE_NONE;

	if (selecting()) {
		view->selection = SELECT_LINES;
		len = prepare_selection();
	} else {
		struct block_iter bi;
		goto_bop();
		bi = view->cursor;
		len = goto_eop(&bi);
	}
	if (!len)
		return;

	sel = do_delete(len);
	get_indent_info(sel, len, &info);
	i = 0;
	while (1) {
		int start;
		int ws_idx = -1;
		int dot = 0;
		int w = 0;

		while (i < len && isspace(sel[i]))
			i++;
		if (i == len)
			break;

		start = i;
		while (i < len) {
			if (isspace(sel[i])) {
				ws_idx = i++;
				while (i < len && isspace(sel[i]))
					i++;
				if (info.width + w + dot + 1 >= text_width) {
					gbuf_add_buf(&buf, sel, info.bytes);
					add_paragraph_line(&buf, sel + start, ws_idx - start);
					w = 0;
					break;
				}
				w += dot + 1;
				dot = 0;
			} else {
				uchar u;
				if (buffer->utf8) {
					u = u_buf_get_char(sel, len, &i);
					w += u_char_width(u);
				} else {
					u = sel[i++];
					w++;
				}
				dot = u == '.' || u == '?' || u == '!';
				if (info.width + w > text_width && ws_idx >= 0) {
					gbuf_add_buf(&buf, sel, info.bytes);
					add_paragraph_line(&buf, sel + start, ws_idx - start);
					w = 0;
					i = ws_idx + 1;
					break;
				}
			}
		}

		if (w) {
			gbuf_add_buf(&buf, sel, info.bytes);
			add_paragraph_line(&buf, sel + start, i - start);
			w = 0;
		}
	}

	if (buf.len)
		do_insert(buf.buffer, buf.len);
	record_replace(sel, len, buf.len);
	move_right(buf.len);
	gbuf_free(&buf);

	update_flags |= UPDATE_FULL;
	select_end();
}
