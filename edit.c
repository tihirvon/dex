#include "buffer.h"
#include "gbuf.h"
#include "util.h"

unsigned int update_flags;

static char *copy_buf;
static unsigned int copy_len;
static int copy_is_lines;

void update_preferred_x(void)
{
	update_cursor_x(view);
	view->preferred_x = view->cx;
}

static void move_preferred_x(void)
{
	struct block_iter bi = view->cursor;
	unsigned int tw = buffer->tab_width;
	int x = 0;

	block_iter_bol(&bi);
	while (x < view->preferred_x) {
		uchar u;

		if (!block_iter_next_uchar(&bi, &u))
			break;
		if (u == '\t') {
			x = (x + tw) / tw * tw;
		} else if (u == '\n') {
			block_iter_prev_byte(&bi, &u);
			break;
		} else if (u < 0x20) {
			x += 2;
		} else {
			x++;
		}
	}
	SET_CURSOR(bi);
}

static void alloc_for_insert(unsigned int len)
{
	struct block *l = view->cursor.blk;
	unsigned int lsize = view->cursor.offset;
	unsigned int rsize = l->size - lsize;

	if (lsize + len <= BLOCK_SIZE && rsize) {
		// merge to left
		struct block *r;

		r = block_new(ALLOC_ROUND(rsize));
		r->size = rsize;
		r->nl = copy_count_nl(r->data, l->data + lsize, rsize);
		list_add_after(&r->node, &l->node);

		l->nl -= r->nl;
		l->size = lsize;
		l->alloc = ALLOC_ROUND(lsize + len);
		xrenew(l->data, l->alloc);
	} else if (rsize + len <= BLOCK_SIZE && lsize) {
		// merge to right
		struct block *r;

		r = block_new(ALLOC_ROUND(rsize + len));
		r->size = rsize;
		r->nl = copy_count_nl(r->data + len, l->data + lsize, rsize);
		list_add_after(&r->node, &l->node);

		l->nl -= r->nl;
		l->size = lsize;
		l->alloc = ALLOC_ROUND(lsize);
		xrenew(l->data, l->alloc);
		view->cursor.blk = r;
		view->cursor.offset = 0;
	} else if (!lsize) {
		if (!rsize) {
			l->alloc = ALLOC_ROUND(len);
			xrenew(l->data, l->alloc);
		} else {
			// don't split, add new block before l
			struct block *m = block_new(len);
			list_add_before(&m->node, &l->node);
			view->cursor.blk = m;
			view->cursor.offset = 0;
		}
	} else {
		struct block *m;

		if (rsize) {
			// split (and add new block between l and r)
			struct block *r = block_new(ALLOC_ROUND(rsize));
			r->size = rsize;
			r->nl = copy_count_nl(r->data, l->data + lsize, rsize);
			list_add_after(&r->node, &l->node);

			l->nl -= r->nl;
			l->size = lsize;
			l->alloc = ALLOC_ROUND(lsize);
			xrenew(l->data, l->alloc);
		}

		// add new block after l
		m = block_new(len);
		list_add_after(&m->node, &l->node);
		view->cursor.blk = m;
		view->cursor.offset = 0;
	}
}

void do_insert(const char *buf, unsigned int len)
{
	struct block *blk = view->cursor.blk;
	unsigned int nl;

	if (len + blk->size > blk->alloc) {
		alloc_for_insert(len);
		blk = view->cursor.blk;
	} else if (view->cursor.offset != blk->size) {
		char *ptr = blk->data + view->cursor.offset;
		memmove(ptr + len, ptr, blk->size - view->cursor.offset);
	}
	nl = copy_count_nl(blk->data + view->cursor.offset, buf, len);
	blk->size += len;
	blk->nl += nl;
	buffer->nl += nl;

	update_flags |= UPDATE_CURSOR_LINE;
	if (nl)
		update_flags |= UPDATE_FULL;
}

void insert(const char *buf, unsigned int len)
{
	record_insert(len);
	do_insert(buf, len);
	update_preferred_x();
}

static unsigned int delete_in_block(unsigned int len)
{
	struct block *blk = view->cursor.blk;
	unsigned int nl, avail;

	if (view->cursor.offset == blk->size) {
		if (blk->node.next == &buffer->blocks)
			return 0;
		blk = BLOCK(blk->node.next);
		view->cursor.blk = blk;
		view->cursor.offset = 0;
	}

	avail = blk->size - view->cursor.offset;
	if (len > avail)
		len = avail;

	nl = count_nl(blk->data + view->cursor.offset, len);
	blk->nl -= nl;
	buffer->nl -= nl;
	if (avail != len) {
		memmove(blk->data + view->cursor.offset,
			blk->data + view->cursor.offset + len,
			avail - len);
	}
	blk->size -= len;
	if (!blk->size) {
		if (blk->node.next != &buffer->blocks) {
			view->cursor.blk = BLOCK(blk->node.next);
			view->cursor.offset = 0;
			delete_block(blk);
		} else if (blk->node.prev != &buffer->blocks) {
			view->cursor.blk = BLOCK(blk->node.prev);
			view->cursor.offset = view->cursor.blk->size;
			delete_block(blk);
		}
	}

	update_flags |= UPDATE_CURSOR_LINE;
	if (nl)
		update_flags |= UPDATE_FULL;
	return len;
}

void do_delete(unsigned int len)
{
	unsigned int deleted = 0;

	while (deleted < len) {
		unsigned int n = delete_in_block(len - deleted);
		if (!n)
			break;
		deleted += n;
	}
}

void delete(unsigned int len, int move_after)
{
	char *buf;

	buf = buffer_get_bytes(&len);
	if (len) {
		record_delete(buf, len, move_after);
		do_delete(len);
		update_preferred_x();
	}
}

void replace(unsigned int del_count, const char *inserted, int ins_count)
{
	char *deleted;

	deleted = buffer_get_bytes(&del_count);
	if (del_count)
		do_delete(del_count);
	if (ins_count)
		do_insert(inserted, ins_count);
	if (del_count || ins_count) {
		record_replace(deleted, del_count, ins_count);
		update_preferred_x();
	}
}

void select_start(int is_lines)
{
	view->sel = view->cursor;
	view->sel_is_lines = is_lines;
	update_flags |= UPDATE_CURSOR_LINE;
}

void select_end(void)
{
	if (view->sel_is_lines)
		move_preferred_x();
	view->sel.blk = NULL;
	view->sel.offset = 0;
	view->sel_is_lines = 0;
	update_flags |= UPDATE_FULL;
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
	char *buf;

	buf = buffer_get_bytes(&len);
	if (len) {
		record_copy(xmemdup(buf, len), len, is_lines);
		record_delete(buf, len, 0);
		do_delete(len);
	}
}

void copy(unsigned int len, int is_lines)
{
	char *buf;

	buf = buffer_get_bytes(&len);
	if (len)
		record_copy(buf, len, is_lines);
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
	if (view->sel.blk) {
		// there is a selection
		struct block_iter bi;
		unsigned int so, co, len;

		so = block_iter_get_offset(&view->sel);
		co = buffer_offset();
		if (co > so) {
			unsigned int to;
			struct block *tb;

			to = co;
			co = so;
			so = to;

			tb = view->cursor.blk;
			view->cursor.blk = view->sel.blk;
			view->sel.blk = tb;

			to = view->cursor.offset;
			view->cursor.offset = view->sel.offset;
			view->sel.offset = to;
		}

		len = so - co;
		if (view->sel_is_lines) {
			bi = view->sel;
			len += count_bytes_eol(&bi);

			bi = view->cursor;
			len += block_iter_bol(&bi);
			SET_CURSOR(bi);
		} else {
			len++;
		}
		return len;
	} else {
		// current line is the selection
		struct block_iter bi = view->cursor;

		block_iter_bol(&bi);
		SET_CURSOR(bi);
		view->sel_is_lines = 1;

		return count_bytes_eol(&bi);
	}
}

void paste(void)
{
	if (view->sel.blk)
		delete_ch();

	undo_merge = UNDO_MERGE_NONE;
	if (!copy_buf)
		return;
	if (copy_is_lines) {
		struct block_iter bi = view->cursor;

		update_preferred_x();
		block_iter_next_line(&bi);
		SET_CURSOR(bi);

		record_insert(copy_len);
		do_insert(copy_buf, copy_len);

		move_preferred_x();
	} else {
		insert(copy_buf, copy_len);
	}
}

void delete_ch(void)
{
	if (view->sel.blk) {
		unsigned int len;

		undo_merge = UNDO_MERGE_NONE;
		len = prepare_selection();
		delete(len, 0);
		select_end();
	} else {
		struct block_iter bi = view->cursor;
		uchar u;

		if (undo_merge != UNDO_MERGE_DELETE)
			undo_merge = UNDO_MERGE_NONE;
		if (buffer->get_char(&bi, &u)) {
			if (buffer->utf8) {
				delete(u_char_size(u), 0);
			} else {
				delete(1, 0);
			}
		}
		undo_merge = UNDO_MERGE_DELETE;
	}
}

void backspace(void)
{
	if (view->sel.blk) {
		unsigned int len;

		undo_merge = UNDO_MERGE_NONE;
		len = prepare_selection();
		delete(len, 1);
		select_end();
	} else {
		struct block_iter bi = view->cursor;
		uchar u;

		if (undo_merge != UNDO_MERGE_BACKSPACE)
			undo_merge = UNDO_MERGE_NONE;
		if (buffer->prev_char(&bi, &u)) {
			SET_CURSOR(bi);
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

	while (1) {
		struct block_iter save;
		int count = 0;
		uchar u;

		block_iter_bol(&bi);
		save = bi;
		while (block_iter_next_byte(&bi, &u) && u != '\n') {
			if (u != ' ' && u != '\t') {
				char *str;
				int i;

				if (!count)
					return NULL;
				bi = save;
				str = xnew(char, count + 1);
				for (i = 0; i < count; i++) {
					block_iter_next_byte(&bi, &u);
					str[i] = u;
				}
				str[i] = 0;
				return str;
			}
			count++;
		}
		bi = save;
		if (!block_iter_prev_line(&bi))
			return NULL;
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
	bi = view->cursor;
	while (block_iter_prev_byte(&bi, &u)) {
		if (u != '\t' && u != ' ') {
			block_iter_next_byte(&bi, &u);
			break;
		}
		count++;
	}

	SET_CURSOR(bi);
	return count;
}

void insert_ch(unsigned int ch)
{
	if (view->sel.blk)
		delete_ch();

	if (undo_merge != UNDO_MERGE_INSERT)
		undo_merge = UNDO_MERGE_NONE;

	if (ch == '\n') {
		char *indent = NULL;
		char *deleted = NULL;
		int ins_count = 0;
		int del_count = 0;

		if (options.auto_indent) {
			indent = get_indent();
			if (indent)
				ins_count += strlen(indent);
		}
		if (options.trim_whitespace) {
			del_count = goto_beginning_of_whitespace();
			if (del_count) {
				deleted = buffer_get_bytes(&del_count);
				do_delete(del_count);
			}
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
		unsigned char buf[5];
		int i = 0;

		if (buffer->utf8) {
			u_set_char_raw(buf, &i, ch);
		} else {
			buf[i++] = ch;
		}
		if (block_iter_eof(&view->cursor))
			buf[i++] = '\n';
		insert(buf, i);
		move_right(1);

		undo_merge = UNDO_MERGE_INSERT;
	}
}

void join_lines(void)
{
	struct block_iter next, bi = view->cursor;
	int count;
	uchar u;
	char *buf;

	if (!block_iter_next_line(&bi)) {
		return;
	}
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
	buf = buffer_get_bytes(&count);
	do_delete(count);
	do_insert(" ", 1);
	record_replace(buf, count, 1);
	update_preferred_x();
}

void move_left(int count)
{
	struct block_iter bi = view->cursor;

	while (count > 0) {
		uchar u;

		if (!buffer->prev_char(&bi, &u))
			break;
		if (!options.move_wraps && u == '\n') {
			block_iter_next_byte(&bi, &u);
			break;
		}
		count--;
	}
	SET_CURSOR(bi);

	update_preferred_x();
}

void move_right(int count)
{
	struct block_iter bi = view->cursor;

	while (count > 0) {
		uchar u;

		if (!buffer->next_char(&bi, &u))
			break;
		if (!options.move_wraps && u == '\n') {
			block_iter_prev_byte(&bi, &u);
			break;
		}
		count--;
	}
	SET_CURSOR(bi);

	update_preferred_x();
}

void move_bol(void)
{
	struct block_iter bi = view->cursor;
	block_iter_bol(&bi);
	SET_CURSOR(bi);
	update_preferred_x();
}

void move_eol(void)
{
	struct block_iter bi = view->cursor;

	while (1) {
		uchar u;

		if (!buffer->next_char(&bi, &u))
			break;
		if (u == '\n') {
			block_iter_prev_byte(&bi, &u);
			break;
		}
	}
	SET_CURSOR(bi);

	update_preferred_x();
}

void move_up(int count)
{
	struct block_iter bi = view->cursor;

	while (count > 0) {
		uchar u;

		if (!buffer->prev_char(&bi, &u))
			break;
		if (u == '\n') {
			count--;
			view->cy--;
		}
	}
	SET_CURSOR(bi);
	move_preferred_x();
}

void move_down(int count)
{
	struct block_iter bi = view->cursor;

	while (count > 0) {
		uchar u;

		if (!buffer->next_char(&bi, &u))
			break;
		if (u == '\n') {
			count--;
			view->cy++;
		}
	}
	SET_CURSOR(bi);
	move_preferred_x();
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

int buffer_get_char(uchar *up)
{
	struct block_iter bi = view->cursor;
	return buffer->next_char(&bi, up);
}

static int is_word_byte(unsigned char byte)
{
	return isalnum(byte) || byte == '_' || byte > 0x7f;
}

char *get_word_under_cursor(void)
{
	struct block_iter bi = view->cursor;
	GBUF(buf);
	uchar ch;

	if (!block_iter_next_byte(&bi, &ch))
		return NULL;

	while (is_word_byte(ch)) {
		if (!block_iter_prev_byte(&bi, &ch))
			break;

	}
	while (!is_word_byte(ch)) {
		if (!block_iter_next_byte(&bi, &ch))
			return NULL;
	}

	do {
		gbuf_add_ch(&buf, ch);
		if (!block_iter_next_byte(&bi, &ch))
			break;
	} while (is_word_byte(ch));
	return gbuf_steal(&buf);
}

void erase_word(void)
{
	struct block_iter bi = view->cursor;
	int count = 0;
	uchar u;

	while (buffer->prev_char(&bi, &u)) {
		if (isspace(u)) {
			count++;
			continue;
		}
		do {
			if (!is_word_byte(u)) {
				if (count)
					buffer->next_char(&bi, &u);
				else
					count++;
				break;
			}
			count += u_char_size(u);
		} while (buffer->prev_char(&bi, &u));
		break;
	}

	if (count) {
		view->cursor = bi;
		delete(count, 1);
	}
}

static char *alloc_indent(int count, int *sizep)
{
	char *indent;
	int size;

	if (options.expand_tab) {
		size = options.indent_width * count;
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

static int get_indent_info(const char *buf, int *sizep, int *levelp)
{
	int pos = 0;
	int width = 0;
	int level = 0;
	// current indentation level
	int cur_spaces = 0;
	int cur_tabs = 0;
	int cur_bytes = 0;
	// totals
	int spaces = 0;
	int tabs = 0;
	int bytes = 0;

	while (buf[pos]) {
		if (buf[pos] == ' ') {
			width++;
			cur_spaces++;
		} else if (buf[pos] == '\t') {
			int tw = options.tab_width;
			width = (width + tw) / tw * tw;
			cur_tabs++;
		} else {
			break;
		}
		cur_bytes++;
		pos++;

		if (width % options.indent_width == 0) {
			spaces += cur_spaces;
			tabs += cur_tabs;
			bytes += cur_bytes;
			level++;
			cur_spaces = 0;
			cur_tabs = 0;
			cur_bytes = 0;
		}
	}

	*sizep = bytes;
	*levelp = level;
	return options.expand_tab ? !tabs : !spaces;
}

static void shift_right(int nr_lines, int count)
{
	int i, indent_size;
	char *indent;

	indent = alloc_indent(count, &indent_size);
	i = 0;
	while (1) {
		int bytes, level;

		fetch_eol(&view->cursor);
		if (get_indent_info(line_buffer, &bytes, &level)) {
			// indent is sane
			// insert whitespace
			do_insert(indent, indent_size);
			record_insert(indent_size);
		} else {
			// replace whole indentation with sane one
			char *deleted;
			char *buf;
			int size;

			deleted = buffer_get_bytes(&bytes);
			do_delete(bytes);
			level += count;

			buf = alloc_indent(level, &size);
			do_insert(buf, size);
			free(buf);
			record_replace(deleted, bytes, size);
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
		int sane, bytes, level;

		fetch_eol(&view->cursor);
		sane = get_indent_info(line_buffer, &bytes, &level);
		if (level && sane) {
			// indent is sane
			char *buf;

			if (level > count)
				level = count;
			if (options.expand_tab)
				level *= options.indent_width;
			buf = buffer_get_bytes(&level);
			do_delete(level);
			record_delete(buf, level, 0);
		} else if (level) {
			// replace whole indentation with sane one
			char *deleted;

			deleted = buffer_get_bytes(&bytes);
			do_delete(bytes);

			if (level > count) {
				char *buf;
				int size;

				buf = alloc_indent(level - count, &size);
				do_insert(buf, size);
				free(buf);
				record_replace(deleted, bytes, size);
			} else {
				record_delete(deleted, bytes, 0);
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
	int sel_offset = 0;

	if (view->sel.blk) {
		struct block_iter si, ei, bi;
		unsigned int so, eo;
		uchar u, prev_char = 0;
		int nr_bytes;

		view->sel_is_lines = 1;

		si = view->cursor;
		ei = view->sel;

		so = block_iter_get_offset(&si);
		eo = block_iter_get_offset(&ei);
		sel_offset = so;
		nr_bytes = eo - so;
		if (so > eo) {
			struct block_iter ti = si;
			si = ei;
			ei = ti;
			SET_CURSOR(si);
			sel_offset = eo;
			nr_bytes = so - eo;
		}
		nr_bytes++;

		bi = si;
		while (nr_bytes && block_iter_next_byte(&bi, &u)) {
			if (prev_char == '\n')
				nr_lines++;
			prev_char = u;
			nr_bytes--;
		}
	}

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

	// make sure sel points to valid block
	if (view->sel.blk)
		block_iter_goto_offset(&view->sel, sel_offset);
}
