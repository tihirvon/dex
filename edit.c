#include "buffer.h"

struct options options = {
	.move_wraps = 1,
};

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
	BLOCK_ITER_CURSOR(bi, view);
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
	view->cblk = bi.blk;
	view->coffset = bi.offset;
}

static void alloc_for_insert(unsigned int len)
{
	struct block *l = view->cblk;
	unsigned int lsize = view->coffset;
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
		view->cblk = r;
		view->coffset = 0;
	} else if (!lsize) {
		if (!rsize) {
			l->alloc = ALLOC_ROUND(len);
			xrenew(l->data, l->alloc);
		} else {
			// don't split, add new block before l
			struct block *m = block_new(len);
			list_add_before(&m->node, &l->node);
			view->cblk = m;
			view->coffset = 0;
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
		view->cblk = m;
		view->coffset = 0;
	}
}

void do_insert(const char *buf, unsigned int len)
{
	struct block *blk = view->cblk;
	unsigned int nl;

	if (len + blk->size > blk->alloc) {
		alloc_for_insert(len);
		blk = view->cblk;
	} else if (view->coffset != blk->size) {
		char *ptr = blk->data + view->coffset;
		memmove(ptr + len, ptr, blk->size - view->coffset);
	}
	nl = copy_count_nl(blk->data + view->coffset, buf, len);
	blk->size += len;
	blk->nl += nl;
	buffer->nl += nl;

	update_flags |= UPDATE_CURSOR_LINE;
	if (nl)
		update_flags |= UPDATE_FULL;
}

void insert(const char *buf, unsigned int len)
{
	record_change(buffer_offset(), NULL, len, 0);
	do_insert(buf, len);
	update_preferred_x();
}

static unsigned int delete_in_block(unsigned int len)
{
	struct block *blk = view->cblk;
	unsigned int nl, avail;

	if (view->coffset == blk->size) {
		if (blk->node.next == &buffer->blocks)
			return 0;
		blk = BLOCK(blk->node.next);
		view->cblk = blk;
		view->coffset = 0;
	}

	avail = blk->size - view->coffset;
	if (len > avail)
		len = avail;

	nl = count_nl(blk->data + view->coffset, len);
	blk->nl -= nl;
	buffer->nl -= nl;
	if (avail != len) {
		memmove(blk->data + view->coffset,
			blk->data + view->coffset + len,
			avail - len);
	}
	blk->size -= len;
	if (!blk->size) {
		if (blk->node.next != &buffer->blocks) {
			view->cblk = BLOCK(blk->node.next);
			view->coffset = 0;
			delete_block(blk);
		} else if (blk->node.prev != &buffer->blocks) {
			view->cblk = BLOCK(blk->node.prev);
			view->coffset = view->cblk->size;
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
		record_change(buffer_offset(), buf, len, move_after);
		do_delete(len);
		update_preferred_x();
	}
}

void select_start(int is_lines)
{
	view->sel_blk = view->cblk;
	view->sel_offset = view->coffset;
	view->sel_is_lines = is_lines;
	update_flags |= UPDATE_CURSOR_LINE;
}

void select_end(void)
{
	if (view->sel_is_lines)
		move_preferred_x();
	view->sel_blk = NULL;
	view->sel_offset = 0;
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
		record_change(buffer_offset(), buf, len, 0);
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
	if (view->sel_blk) {
		// there is a selection
		struct block_iter bi;
		unsigned int so, co, len;

		so = buffer_get_offset(view->sel_blk, view->sel_offset);
		co = buffer_offset();
		if (co > so) {
			unsigned int to;
			struct block *tb;

			to = co;
			co = so;
			so = to;

			tb = view->cblk;
			view->cblk = view->sel_blk;
			view->sel_blk = tb;

			to = view->coffset;
			view->coffset = view->sel_offset;
			view->sel_offset = to;
		}

		len = so - co;
		if (view->sel_is_lines) {
			bi.head = &view->buffer->blocks;
			bi.blk = view->sel_blk;
			bi.offset = view->sel_offset;
			len += count_bytes_eol(&bi);

			init_block_iter_cursor(&bi, view);
			len += block_iter_bol(&bi);
			view->cblk = bi.blk;
			view->coffset = bi.offset;
		} else {
			len++;
		}
		return len;
	} else {
		// current line is the selection
		BLOCK_ITER_CURSOR(bi, view);

		block_iter_bol(&bi);
		view->cblk = bi.blk;
		view->coffset = bi.offset;
		view->sel_is_lines = 1;

		return count_bytes_eol(&bi);
	}
}

void paste(void)
{
	if (view->sel_blk)
		delete_ch();

	undo_merge = UNDO_MERGE_NONE;
	if (!copy_buf)
		return;
	if (copy_is_lines) {
		BLOCK_ITER_CURSOR(bi, view);

		update_preferred_x();
		block_iter_next_line(&bi);
		view->cblk = bi.blk;
		view->coffset = bi.offset;

		record_change(buffer_offset(), NULL, copy_len, 0);
		do_insert(copy_buf, copy_len);

		move_preferred_x();
	} else {
		insert(copy_buf, copy_len);
	}
}

void delete_ch(void)
{
	if (view->sel_blk) {
		unsigned int len;

		undo_merge = UNDO_MERGE_NONE;
		len = prepare_selection();
		delete(len, 0);
		select_end();
	} else {
		BLOCK_ITER_CURSOR(bi, view);
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
	if (view->sel_blk) {
		unsigned int len;

		undo_merge = UNDO_MERGE_NONE;
		len = prepare_selection();
		delete(len, 1);
		select_end();
	} else {
		BLOCK_ITER_CURSOR(bi, view);
		uchar u;

		if (undo_merge != UNDO_MERGE_BACKSPACE)
			undo_merge = UNDO_MERGE_NONE;
		if (buffer->prev_char(&bi, &u)) {
			view->cblk = bi.blk;
			view->coffset = bi.offset;
			if (buffer->utf8) {
				delete(u_char_size(u), 1);
			} else {
				delete(1, 1);
			}
		}
		undo_merge = UNDO_MERGE_BACKSPACE;
	}
}

void insert_ch(unsigned int ch)
{
	unsigned char buf[5];
	int i = 0;

	if (view->sel_blk)
		delete_ch();

	if (undo_merge != UNDO_MERGE_INSERT)
		undo_merge = UNDO_MERGE_NONE;

	if (buffer->utf8) {
		u_set_char_raw(buf, &i, ch);
	} else {
		buf[i++] = ch;
	}
	if (ch != '\n' && view->cblk->node.next == &buffer->blocks && view->coffset == view->cblk->size)
		buf[i++] = '\n';
	insert(buf, i);
	move_right(1);

	undo_merge = UNDO_MERGE_INSERT;
	if (ch == '\n')
		undo_merge = UNDO_MERGE_NONE;
}

void move_left(int count)
{
	BLOCK_ITER_CURSOR(bi, view);

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
	view->cblk = bi.blk;
	view->coffset = bi.offset;

	update_preferred_x();
}

void move_right(int count)
{
	BLOCK_ITER_CURSOR(bi, view);

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
	view->cblk = bi.blk;
	view->coffset = bi.offset;

	update_preferred_x();
}

void move_bol(void)
{
	BLOCK_ITER_CURSOR(bi, view);
	block_iter_bol(&bi);
	view->cblk = bi.blk;
	view->coffset = bi.offset;
	update_preferred_x();
}

void move_eol(void)
{
	BLOCK_ITER_CURSOR(bi, view);

	while (1) {
		uchar u;

		if (!buffer->next_char(&bi, &u))
			break;
		if (u == '\n') {
			block_iter_prev_byte(&bi, &u);
			break;
		}
	}
	view->cblk = bi.blk;
	view->coffset = bi.offset;

	update_preferred_x();
}

void move_up(int count)
{
	BLOCK_ITER_CURSOR(bi, view);

	while (count > 0) {
		uchar u;

		if (!buffer->prev_char(&bi, &u))
			break;
		if (u == '\n') {
			count--;
			view->cy--;
		}
	}
	view->cblk = bi.blk;
	view->coffset = bi.offset;
	move_preferred_x();
}

void move_down(int count)
{
	BLOCK_ITER_CURSOR(bi, view);

	while (count > 0) {
		uchar u;

		if (!buffer->next_char(&bi, &u))
			break;
		if (u == '\n') {
			count--;
			view->cy++;
		}
	}
	view->cblk = bi.blk;
	view->coffset = bi.offset;
	move_preferred_x();
}

void move_bof(void)
{
	view->cblk = BLOCK(buffer->blocks.next);
	view->coffset = 0;
	move_preferred_x();
}

void move_eof(void)
{
	view->cblk = BLOCK(buffer->blocks.prev);
	view->coffset = view->cblk->size;
}

int buffer_get_char(uchar *up)
{
	BLOCK_ITER_CURSOR(bi, view);
	return buffer->next_char(&bi, up);
}
