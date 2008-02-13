#include "buffer.h"

int move_wraps = 1;
unsigned int update_flags;

static char *copy_buf;
static unsigned int copy_len;
static int copy_is_lines;

void update_preferred_x(void)
{
	update_cursor_x(window);
	window->preferred_x = window->cx;
}

static void move_preferred_x(void)
{
	BLOCK_ITER_CURSOR(bi, window);
	unsigned int tw = buffer->tab_width;
	int x = 0;

	block_iter_bol(&bi);
	while (x < window->preferred_x) {
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
	window->cblk = bi.blk;
	window->coffset = bi.offset;
}

static void alloc_for_insert(unsigned int len)
{
	struct block *l = window->cblk;
	unsigned int lsize = window->coffset;
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
		window->cblk = r;
		window->coffset = 0;
	} else if (!lsize) {
		// don't split, add new block before l
		struct block *m = block_new(len);
		list_add_before(&m->node, &l->node);
		window->cblk = m;
		window->coffset = 0;
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
		window->cblk = m;
		window->coffset = 0;
	}
}

void do_insert(const char *buf, unsigned int len)
{
	struct block *blk = window->cblk;
	unsigned int nl;

	if (len + blk->size > blk->alloc) {
		alloc_for_insert(len);
		blk = window->cblk;
	} else if (window->coffset != blk->size) {
		char *ptr = blk->data + window->coffset;
		memmove(ptr + len, ptr, blk->size - window->coffset);
	}
	nl = copy_count_nl(blk->data + window->coffset, buf, len);
	blk->size += len;
	blk->nl += nl;
	buffer->nl += nl;

	update_flags |= UPDATE_CURSOR_LINE;
	if (nl)
		update_flags |= UPDATE_FULL;
}

void insert(const char *buf, unsigned int len)
{
	record_change(buffer_offset(), NULL, len);
	do_insert(buf, len);
	update_preferred_x();
}

static unsigned int delete_in_block(unsigned int len)
{
	struct block *blk = window->cblk;
	unsigned int nl, avail;

	if (window->coffset == blk->size) {
		if (blk->node.next == &buffer->blocks)
			return 0;
		blk = BLOCK(blk->node.next);
		window->cblk = blk;
		window->coffset = 0;
	}

	avail = blk->size - window->coffset;
	if (len > avail)
		len = avail;

	nl = count_nl(blk->data + window->coffset, len);
	blk->nl -= nl;
	buffer->nl -= nl;
	if (avail != len) {
		memmove(blk->data + window->coffset,
			blk->data + window->coffset + len,
			avail - len);
	}
	blk->size -= len;
	if (!blk->size) {
		if (blk->node.next != &buffer->blocks) {
			window->cblk = BLOCK(blk->node.next);
			window->coffset = 0;
			delete_block(blk);
		} else if (blk->node.prev != &buffer->blocks) {
			window->cblk = BLOCK(blk->node.prev);
			window->coffset = window->cblk->size;
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

static void delete(unsigned int len)
{
	char *buf;

	buf = buffer_get_bytes(&len);
	if (len) {
		record_change(buffer_offset(), buf, len);
		do_delete(len);
		update_preferred_x();
	}
}

void select_start(int is_lines)
{
	window->sel_blk = window->cblk;
	window->sel_offset = window->coffset;
	window->sel_is_lines = is_lines;
}

void select_end(void)
{
	if (window->sel_is_lines)
		move_preferred_x();
	window->sel_blk = NULL;
	window->sel_offset = 0;
	window->sel_is_lines = 0;
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
		record_change(buffer_offset(), buf, len);
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

static unsigned int count_bytes_eol(struct block_iter *bi)
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
	if (window->sel_blk) {
		// there is a selection
		struct block_iter bi;
		unsigned int so, co, len;

		so = buffer_get_offset(window->sel_blk, window->sel_offset);
		co = buffer_offset();
		if (co > so) {
			unsigned int to;
			struct block *tb;

			to = co;
			co = so;
			so = to;

			tb = window->cblk;
			window->cblk = window->sel_blk;
			window->sel_blk = tb;

			to = window->coffset;
			window->coffset = window->sel_offset;
			window->sel_offset = to;
		}

		len = so - co;
		if (window->sel_is_lines) {
			bi.head = &window->buffer->blocks;
			bi.blk = window->sel_blk;
			bi.offset = window->sel_offset;
			len += count_bytes_eol(&bi);

			init_block_iter_cursor(&bi, window);
			len += block_iter_bol(&bi);
			window->cblk = bi.blk;
			window->coffset = bi.offset;
		} else {
			len++;
		}
		return len;
	} else {
		// current line is the selection
		BLOCK_ITER_CURSOR(bi, window);

		block_iter_bol(&bi);
		window->cblk = bi.blk;
		window->coffset = bi.offset;

		return count_bytes_eol(&bi);
	}
}

void paste(void)
{
	undo_merge = UNDO_MERGE_NONE;
	if (!copy_buf)
		return;
	if (copy_is_lines) {
		BLOCK_ITER_CURSOR(bi, window);

		update_preferred_x();
		block_iter_next_line(&bi);
		window->cblk = bi.blk;
		window->coffset = bi.offset;

		record_change(buffer_offset(), NULL, copy_len);
		do_insert(copy_buf, copy_len);

		move_preferred_x();
	} else {
		insert(copy_buf, copy_len);
	}
}

void delete_ch(void)
{
	BLOCK_ITER_CURSOR(bi, window);
	uchar u;

	if (undo_merge != UNDO_MERGE_DELETE)
		undo_merge = UNDO_MERGE_NONE;
	if (buffer->get_char(&bi, &u)) {
		if (buffer->utf8) {
			delete(u_char_size(u));
		} else {
			delete(1);
		}
	}
	undo_merge = UNDO_MERGE_DELETE;
}

void backspace(void)
{
	BLOCK_ITER_CURSOR(bi, window);
	uchar u;

	if (undo_merge != UNDO_MERGE_BACKSPACE)
		undo_merge = UNDO_MERGE_NONE;
	if (buffer->prev_char(&bi, &u)) {
		window->cblk = bi.blk;
		window->coffset = bi.offset;
		if (buffer->utf8) {
			delete(u_char_size(u));
		} else {
			delete(1);
		}
	}
	undo_merge = UNDO_MERGE_BACKSPACE;
}

void insert_ch(unsigned int ch)
{
	unsigned char buf[4];
	int i = 0;

	if (undo_merge != UNDO_MERGE_INSERT)
		undo_merge = UNDO_MERGE_NONE;

	if (buffer->utf8) {
		u_set_char_raw(buf, &i, ch);
	} else {
		buf[i++] = ch;
	}
	insert(buf, i);
	move_right(1);

	undo_merge = UNDO_MERGE_INSERT;
	if (ch == '\n')
		undo_merge = UNDO_MERGE_NONE;
}

void move_left(int count)
{
	BLOCK_ITER_CURSOR(bi, window);

	while (count > 0) {
		uchar u;

		if (!buffer->prev_char(&bi, &u))
			break;
		if (!move_wraps && u == '\n') {
			block_iter_next_byte(&bi, &u);
			break;
		}
		count--;
	}
	window->cblk = bi.blk;
	window->coffset = bi.offset;

	update_preferred_x();
}

void move_right(int count)
{
	BLOCK_ITER_CURSOR(bi, window);

	while (count > 0) {
		uchar u;

		if (!buffer->next_char(&bi, &u))
			break;
		if (!move_wraps && u == '\n') {
			block_iter_prev_byte(&bi, &u);
			break;
		}
		count--;
	}
	window->cblk = bi.blk;
	window->coffset = bi.offset;

	update_preferred_x();
}

void move_bol(void)
{
	BLOCK_ITER_CURSOR(bi, window);
	block_iter_bol(&bi);
	window->cblk = bi.blk;
	window->coffset = bi.offset;
	update_preferred_x();
}

void move_eol(void)
{
	BLOCK_ITER_CURSOR(bi, window);

	while (1) {
		uchar u;

		if (!buffer->next_char(&bi, &u))
			break;
		if (u == '\n') {
			block_iter_prev_byte(&bi, &u);
			break;
		}
	}
	window->cblk = bi.blk;
	window->coffset = bi.offset;

	update_preferred_x();
}

void move_up(int count)
{
	BLOCK_ITER_CURSOR(bi, window);

	while (count > 0) {
		uchar u;

		if (!buffer->prev_char(&bi, &u))
			break;
		if (u == '\n') {
			count--;
			window->cy--;
		}
	}
	window->cblk = bi.blk;
	window->coffset = bi.offset;
	move_preferred_x();
}

void move_down(int count)
{
	BLOCK_ITER_CURSOR(bi, window);

	while (count > 0) {
		uchar u;

		if (!buffer->next_char(&bi, &u))
			break;
		if (u == '\n') {
			count--;
			window->cy++;
		}
	}
	window->cblk = bi.blk;
	window->coffset = bi.offset;
	move_preferred_x();
}

int buffer_get_char(uchar *up)
{
	BLOCK_ITER_CURSOR(bi, window);
	return buffer->next_char(&bi, up);
}
