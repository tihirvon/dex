#include "buffer.h"
#include "buffer-highlight.h"
#include "change.h"
#include "edit.h"
#include "gbuf.h"

unsigned int update_flags;

static char *copy_buf;
static unsigned int copy_len;
static int copy_is_lines;

void update_preferred_x(void)
{
	update_cursor_x();
	view->preferred_x = view->cx_display;
}

void move_preferred_x(void)
{
	unsigned int tw = buffer->options.tab_width;
	int in_space_indent = 1;
	int x = 0;
	uchar u;

	block_iter_bol(&view->cursor);
	while (x < view->preferred_x) {
		if (!block_iter_next_uchar(&view->cursor, &u))
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

static unsigned int insert_to_current(const char *buf, unsigned int len)
{
	struct block *blk = view->cursor.blk;
	unsigned int offset = view->cursor.offset;
	unsigned int size = blk->size + len;
	unsigned int nl;

	if (size > blk->alloc) {
		blk->alloc = ALLOC_ROUND(size);
		xrenew(blk->data, blk->alloc);
	}
	memmove(blk->data + offset + len, blk->data + offset, blk->size - offset);
	nl = copy_count_nl(blk->data + offset, buf, len);
	blk->nl += nl;
	blk->size = size;
	return nl;
}

static unsigned int insert_to_next(const char *buf, unsigned int len)
{
	BUG_ON(view->cursor.offset != view->cursor.blk->size);
	BUG_ON(view->cursor.blk->node.next == &buffer->blocks);
	view->cursor.blk = BLOCK(view->cursor.blk->node.next);
	view->cursor.offset = 0;
	return insert_to_current(buf, len);
}

static unsigned int append_to_current(const char *buf, unsigned int len)
{
	struct block *blk = view->cursor.blk;
	unsigned int size = blk->size + len;
	unsigned int nl;

	if (size > blk->alloc) {
		blk->alloc = ALLOC_ROUND(size);
		xrenew(blk->data, blk->alloc);
	}
	nl = copy_count_nl(blk->data + blk->size, buf, len);
	blk->nl += nl;
	blk->size = size;
	return nl;
}

static unsigned int add_new_block(const char *buf, unsigned int len)
{
	struct block *blk = block_new(ALLOC_ROUND(len));

	blk->nl = copy_count_nl(blk->data, buf, len);
	blk->size = len;
	list_add_after(&blk->node, &view->cursor.blk->node);
	return blk->nl;
}

static unsigned int insert_bytes(const char *buf, unsigned int len)
{
	struct block *blk = view->cursor.blk;
	struct block *next = NULL;
	unsigned int offset = view->cursor.offset;

	if (offset < blk->size)
		return insert_to_current(buf, len);

	if (!blk->size || blk->data[blk->size - 1] != '\n') {
		// must append to this block
		return append_to_current(buf, len);
	}

	if (blk->node.next != &buffer->blocks)
		next = BLOCK(blk->node.next);

	if (buf[len - 1] != '\n' && next) {
		// must insert to beginning of next block
		return insert_to_next(buf, len);
	}

	if (blk->size + len > BLOCK_MAX_SIZE) {
		// this block would grow too big, insert to next or add new?
		if (next && len + next->size <= BLOCK_MAX_SIZE) {
			// fits to next block
			return insert_to_next(buf, len);
		}
		return add_new_block(buf, len);
	}

	// fits to this block
	return append_to_current(buf, len);
}

void do_insert(const char *buf, unsigned int len)
{
	unsigned int nl = insert_bytes(buf, len);

	buffer->nl += nl;
	update_flags |= UPDATE_CURSOR_LINE;
	if (nl)
		update_flags |= UPDATE_FULL;

	update_hl_insert(nl, len);
}

void insert(const char *buf, unsigned int len)
{
	record_insert(len);
	do_insert(buf, len);
	update_preferred_x();
}

static int only_block(struct block *blk)
{
	return blk->node.prev == &buffer->blocks && blk->node.next == &buffer->blocks;
}

char *do_delete(unsigned int len)
{
	struct list_head *saved_prev_node = NULL;
	struct block *blk = view->cursor.blk;
	unsigned int buffer_nl = buffer->nl;
	unsigned int offset = view->cursor.offset;
	unsigned int pos = 0;
	char *buf;

	if (!len)
		return NULL;

	if (!offset) {
		// the block where cursor is can become empty and thereby may be deleted
		saved_prev_node = blk->node.prev;
	}

	buf = xnew(char, len);
	while (pos < len) {
		struct list_head *next = blk->node.next;
		unsigned int avail = blk->size - offset;
		unsigned int count = len - pos;
		unsigned int nl;

		if (count > avail)
			count = avail;
		nl = copy_count_nl(buf + pos, blk->data + offset, count);
		if (count < avail)
			memmove(blk->data + offset, blk->data + offset + count, avail - count);

		buffer->nl -= nl;
		blk->nl -= nl;
		blk->size -= count;
		if (!blk->size && !only_block(blk))
			delete_block(blk);

		offset = 0;
		pos += count;
		blk = BLOCK(next);

		BUG_ON(pos < len && next == &buffer->blocks);
	}

	if (saved_prev_node)
		view->cursor.blk = BLOCK(saved_prev_node->next);

	blk = view->cursor.blk;
	if (blk->size && blk->data[blk->size - 1] != '\n' && blk->node.next != &buffer->blocks) {
		struct block *next = BLOCK(blk->node.next);
		unsigned int size = blk->size + next->size;

		if (size > blk->alloc) {
			blk->alloc = ALLOC_ROUND(size);
			xrenew(blk->data, blk->alloc);
		}
		memcpy(blk->data + blk->size, next->data, next->size);
		blk->size = size;
		blk->nl += next->nl;
		delete_block(next);
	}

	update_flags |= UPDATE_CURSOR_LINE;
	if (buffer_nl != buffer->nl)
		update_flags |= UPDATE_FULL;

	update_hl_insert(0, -len);
	return buf;
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
		view->sel_is_lines = 0;
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

unsigned int select_current_line(void)
{
	struct block_iter bi;

	block_iter_bol(&view->cursor);
	view->sel_is_lines = 1;

	bi = view->cursor;
	return count_bytes_eol(&bi);
}

void paste(void)
{
	if (view->sel.blk)
		delete_ch();

	undo_merge = UNDO_MERGE_NONE;
	if (!copy_buf)
		return;
	if (copy_is_lines) {
		update_preferred_x();
		block_iter_next_line(&view->cursor);

		record_insert(copy_len);
		do_insert(copy_buf, copy_len);

		move_preferred_x();
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

static int get_indent_level_bytes_left(void)
{
	struct block_iter bi = view->cursor;
	int cursor_offset = block_iter_bol(&bi);
	int ibytes;

	if (!cursor_offset)
		return 0;

	fetch_eol(&bi);
	ibytes = get_current_indent_bytes(line_buffer, cursor_offset);
	if (ibytes < 0)
		return 0;
	return ibytes;
}

static int get_indent_level_bytes_right(void)
{
	struct block_iter bi = view->cursor;
	int cursor_offset = block_iter_bol(&bi);
	int tw = buffer->options.tab_width;
	int i, ibytes, iwidth;

	fetch_eol(&bi);
	ibytes = get_current_indent_bytes(line_buffer, cursor_offset);
	if (ibytes < 0)
		return 0;

	iwidth = 0;
	for (i = cursor_offset; ; i++) {
		char ch = line_buffer[i];

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
}

static void delete_one_ch(void)
{
	struct block_iter bi = view->cursor;
	uchar u;

	if (!buffer->next_char(&bi, &u))
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
	if (view->sel.blk) {
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
	if (view->sel.blk) {
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

		if (buffer->prev_char(&view->cursor, &u)) {
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
	if (view->sel.blk)
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
		unsigned char buf[8];
		int chars = 1;
		int i = 0;

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
	unsigned int count, len = 0, join = 0, del = 0;
	struct selection_info info;
	struct block_iter bi;
	uchar ch = 0;

	init_selection(&info);
	count = info.eo - info.so;
	bi = info.si;
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

	block_iter_goto_offset(&view->sel, info.so);
	block_iter_goto_offset(&view->cursor, info.eo - del - 1);
}

void join_lines(void)
{
	struct block_iter next, bi = view->cursor;
	int count;
	uchar u;
	char *buf;

	if (view->sel.blk) {
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

void move_left(int count)
{
	while (count > 0) {
		uchar u;

		if (!buffer->prev_char(&view->cursor, &u))
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

		if (!buffer->next_char(&view->cursor, &u))
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
	while (1) {
		uchar u;

		if (!buffer->next_char(&view->cursor, &u))
			break;
		if (u == '\n') {
			block_iter_prev_byte(&view->cursor, &u);
			break;
		}
	}
	update_preferred_x();
}

void move_up(int count)
{
	while (count > 0) {
		uchar u;

		if (!buffer->prev_char(&view->cursor, &u))
			break;
		if (u == '\n') {
			count--;
			view->cy--;
		}
	}
	move_preferred_x();
}

void move_down(int count)
{
	while (count > 0) {
		uchar u;

		if (!buffer->next_char(&view->cursor, &u))
			break;
		if (u == '\n') {
			count--;
			view->cy++;
		}
	}
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
	int si, ei;

	block_iter_bol(&bi);
	fetch_eol(&bi);

	si = view->cx;
	while (!is_word_byte(line_buffer[si])) {
		if (!line_buffer[si])
			return NULL;
		si++;
	}
	ei = si;
	while (si > 0 && is_word_byte(line_buffer[si - 1]))
		si--;
	while (is_word_byte(line_buffer[ei + 1]))
		ei++;
	return xstrndup(line_buffer + si, ei - si + 1);
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

		fetch_eol(&view->cursor);
		get_indent_info(line_buffer, line_buffer_len, &info);
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

		fetch_eol(&view->cursor);
		get_indent_info(line_buffer, line_buffer_len, &info);
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

	if (view->sel.blk) {
		view->sel_is_lines = 1;
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

	if (view->sel.blk) {
		// make sure sel points to valid block
		block_iter_goto_offset(&view->sel, info.so);

		// restore cursor position as well as possible
		if (!info.swapped) {
			struct block_iter tmp = view->sel;
			view->sel = view->cursor;
			view->cursor = tmp;
		}
	}
	move_preferred_x();
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
		block_iter_bol(&view->cursor);
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
	unsigned int len;
	char *sel;
	int i;
	GBUF(buf);

	undo_merge = UNDO_MERGE_NONE;

	if (view->sel.blk) {
		view->sel_is_lines = 1;
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
					u = u_get_char(sel, &i);
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
