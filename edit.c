#include "edit.h"
#include "move.h"
#include "buffer.h"
#include "change.h"
#include "block.h"
#include "gbuf.h"
#include "indent.h"
#include "uchar.h"
#include "regexp.h"

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
	unsigned int rec_len = len;

	if (len == 0)
		return;

	if (buf[len - 1] != '\n' && block_iter_is_eof(&view->cursor)) {
		// force newline at EOF
		do_insert("\n", 1);
		rec_len++;
	}

	do_insert(buf, len);
	record_insert(rec_len);
}

void delete(unsigned int len, int move_after)
{
	char *buf;

	if (len == 0)
		return;

	buf = do_delete(len);
	if (block_iter_is_eof(&view->cursor)) {
		struct block_iter bi = view->cursor;
		unsigned int u;
		if (block_iter_prev_byte(&bi, &u) && u != '\n') {
			// deleted last newline from EOF
			do_insert("\n", 1);
			if (--len == 0) {
				begin_change(CHANGE_MERGE_NONE);
				free(buf);
				return;
			}
		}
	}
	record_delete(buf, len, move_after);
}

static int would_delete_last_bytes(unsigned int count)
{
	struct block *blk = view->cursor.blk;
	unsigned int offset = view->cursor.offset;

	while (1) {
		unsigned int avail = blk->size - offset;

		if (avail > count)
			return 0;

		if (blk->node.next == view->cursor.head)
			return 1;

		count -= avail;
		blk = BLOCK(blk->node.next);
		offset = 0;
	}
}

void replace(unsigned int del_count, const char *inserted, int ins_count)
{
	char *deleted = NULL;

	if (del_count == 0) {
		insert(inserted, ins_count);
		return;
	}
	if (ins_count == 0) {
		delete(del_count, 0);
		return;
	}

	// check if all newlines from EOF would be deleted
	if (would_delete_last_bytes(del_count)) {
		if (inserted[ins_count - 1] != '\n') {
			// don't replace last newline
			if (--del_count == 0) {
				insert(inserted, ins_count);
				return;
			}
		}
	}

	deleted = do_replace(del_count, inserted, ins_count);
	record_replace(deleted, del_count, ins_count);
}

/*
 * Stupid { ... } block selector.
 *
 * Because braces can be inside strings or comments and writing real
 * parser for many programming languages does not make sense the rules
 * for selecting a block are made very simple. Line that matches \{\s*$
 * starts a block and line that matches ^\s*\} ends it.
 */
void select_block(void)
{
	const char *spattern = "\\{\\s*$";
	const char *epattern = "^\\s*\\}";
	struct block_iter sbi, ebi, bi = view->cursor;
	struct lineref lr;
	int level = 0;

	// If current line does not match \{\s*$ but matches ^\s*\} then
	// cursor is likely at end of the block you want to select.
	fetch_this_line(&bi, &lr);
	if (!regexp_match_nosub(spattern, lr.line, lr.size) &&
	     regexp_match_nosub(epattern, lr.line, lr.size))
		block_iter_prev_line(&bi);

	while (1) {
		fetch_this_line(&bi, &lr);
		if (regexp_match_nosub(spattern, lr.line, lr.size)) {
			if (level++ == 0) {
				sbi = bi;
				block_iter_next_line(&bi);
				break;
			}
		}
		if (regexp_match_nosub(epattern, lr.line, lr.size))
			level--;

		if (!block_iter_prev_line(&bi))
			return;
	}

	while (1) {
		fetch_this_line(&bi, &lr);
		if (regexp_match_nosub(epattern, lr.line, lr.size)) {
			if (--level == 0) {
				ebi = bi;
				break;
			}
		}
		if (regexp_match_nosub(spattern, lr.line, lr.size))
			level++;

		if (!block_iter_next_line(&bi))
			return;
	}

	view->cursor = sbi;
	view->sel_so = block_iter_get_offset(&ebi);
	view->sel_eo = UINT_MAX;
	view->selection = SELECT_LINES;

	update_flags |= UPDATE_FULL;
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
		char *buf = buffer_get_bytes(len);
		record_copy(buf, len, is_lines);
		delete(len, 0);
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

void insert_text(const char *text, unsigned int size)
{
	unsigned int del_count = 0;
	unsigned int skip = size;
	char *del = NULL;

	// prepare deleted text (selection)
	if (selecting()) {
		del_count = prepare_selection();
		unselect();
	}

	// modify
	if (del_count)
		del = do_delete(del_count);
	if (block_iter_is_eof(&view->cursor) && text[size - 1] != '\n') {
		// this appears to be in wrong order but because the cursor
		// doesn't move the result is correct
		do_insert("\n", 1);
		do_insert(text, size);
		size++;
	} else {
		do_insert(text, size);
	}

	// record change
	record_replace(del, del_count, size);

	// move after inserted text
	block_iter_skip_bytes(&view->cursor, skip);
	update_preferred_x();
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

void delete_ch(void)
{
	unsigned int u, size = 0;

	if (selecting()) {
		size = prepare_selection();
		unselect();
	} else {
		begin_change(CHANGE_MERGE_DELETE);
		if (buffer->options.emulate_tab)
			size = get_indent_level_bytes_right();
		if (size == 0)
			size = buffer_get_char(&view->cursor, &u);
	}
	delete(size, 0);
	update_preferred_x();
}

void erase(void)
{
	unsigned int u, size = 0;

	if (selecting()) {
		size = prepare_selection();
		unselect();
	} else {
		begin_change(CHANGE_MERGE_ERASE);
		if (buffer->options.emulate_tab) {
			size = get_indent_level_bytes_left();
			block_iter_back_bytes(&view->cursor, size);
		}
		if (size == 0)
			size = buffer_prev_char(&view->cursor, &u);
	}
	delete(size, 1);
	update_preferred_x();
}

// goto beginning of whitespace (tabs and spaces) under cursor and
// return number of whitespace bytes after cursor after moving cursor
static unsigned int goto_beginning_of_whitespace(void)
{
	struct block_iter bi = view->cursor;
	unsigned int count = 0;
	unsigned int u;

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

static void insert_nl(void)
{
	unsigned int ins_count = 0;
	unsigned int del_count = 0;
	char *deleted = NULL;
	char *indent = NULL;

	// prepare deleted text (selection or whitespace around cursor)
	if (selecting()) {
		del_count = prepare_selection();
		unselect();
	} else if (buffer->options.trim_whitespace) {
		del_count = goto_beginning_of_whitespace();
	}

	// prepare inserted indentation
	if (buffer->options.auto_indent) {
		indent = get_indent();
		if (indent)
			ins_count = strlen(indent);
	}

	// modify
	if (del_count)
		deleted = do_delete(del_count);
	if (ins_count) {
		do_insert(indent, ins_count);
		free(indent);
	}
	do_insert("\n", 1);
	ins_count++;

	// record change
	begin_change(CHANGE_MERGE_NONE);
	record_replace(deleted, del_count, ins_count);
	end_change();

	// move after inserted text
	block_iter_skip_bytes(&view->cursor, ins_count);
	update_preferred_x();
}

void insert_ch(unsigned int ch)
{
	unsigned int del_count = 0;
	unsigned int ins_count = 0;
	unsigned int skip;
	char *del = NULL;
	char ins[9];

	if (ch == '\n') {
		insert_nl();
		return;
	}

	// prepare deleted text (selection)
	if (selecting()) {
		del_count = prepare_selection();
		unselect();
	}

	// prepare inserted text
	if (ch == '\t' && buffer->options.expand_tab) {
		ins_count = buffer->options.indent_width;
		memset(ins, ' ', ins_count);
	} else if (buffer->options.utf8) {
		u_set_char_raw(ins, &ins_count, ch);
	} else {
		ins[ins_count++] = ch;
	}
	skip = ins_count;

	// modify
	if (del_count)
		del = do_delete(del_count);
	if (block_iter_is_eof(&view->cursor))
		ins[ins_count++] = '\n';
	do_insert(ins, ins_count);

	// record change
	if (del_count) {
		begin_change(CHANGE_MERGE_NONE);
		record_replace(del, del_count, ins_count);
	} else {
		begin_change(CHANGE_MERGE_INSERT);
		record_insert(ins_count);
	}
	end_change();

	// move after inserted text
	block_iter_skip_bytes(&view->cursor, skip);
	update_preferred_x();
}

static void join_selection(void)
{
	unsigned int count = prepare_selection();
	unsigned int len = 0, join = 0;
	struct block_iter bi;
	unsigned int ch = 0;

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
	unsigned int u;
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
		unsigned int u;

		if (!buffer_get_char(&view->cursor, &u))
			return;

		text_len = 1;
		if (buffer->options.utf8)
			text_len = u_char_size(u);
	}

	src = do_delete(text_len);
	i = 0;
	while (i < text_len) {
		unsigned int u;

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
	update_preferred_x();
}
