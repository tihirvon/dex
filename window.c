#include "buffer.h"

struct window *window;

struct window *window_new(void)
{
	struct window *w = xnew0(struct window, 1);

	w->w = 80;
	w->h = 24;
	return w;
}

void window_add_buffer(struct buffer *b)
{
	xrenew(window->buffers, window->nr_buffers + 1);
	window->buffers[window->nr_buffers++] = b;
}

void set_buffer(struct buffer *b)
{
	window->buffer = b;
	window->cblk = BLOCK(b->blocks.next);
	buffer = b;
	update_flags |= UPDATE_FULL;
}

void next_buffer(void)
{
	int i;

	for (i = 0; i < window->nr_buffers; i++) {
		if (window->buffer == window->buffers[i])
			break;
	}
	i = (i + 1) % window->nr_buffers;
	set_buffer(window->buffers[i]);
}

void prev_buffer(void)
{
	int i;

	for (i = 0; i < window->nr_buffers; i++) {
		if (window->buffer == window->buffers[i])
			break;
	}
	i = (i + window->nr_buffers - 1) % window->nr_buffers;
	set_buffer(window->buffers[i]);
}

static void update_cursor_y(struct window *w)
{
	struct block *blk;
	unsigned int nl = 0;

	list_for_each_entry(blk, &w->buffer->blocks, node) {
		if (blk == w->cblk) {
			nl += count_nl(blk->data, w->coffset);
			w->cy = nl;
			return;
		}
		nl += blk->nl;
	}
	BUG_ON(1);
}

void update_cursor_x(struct window *w)
{
	BLOCK_ITER_CURSOR(bi, w);
	unsigned int tw = w->buffer->tab_width;

	block_iter_bol(&bi);
	w->cx = 0;
	w->cx_idx = 0;
	while (1) {
		uchar u;

		if (bi.blk == w->cblk && bi.offset == w->coffset)
			break;
		if (!w->coffset && bi.offset == bi.blk->size && bi.blk->node.next == &w->cblk->node) {
			// this[this.size] == this.next[0]
			break;
		}
		if (!w->buffer->next_char(&bi, &u))
			break;
		w->cx_idx++;
		if (u == '\t') {
			w->cx = (w->cx + tw) / tw * tw;
		} else if (u < 0x20) {
			w->cx += 2;
		} else {
			w->cx += u_char_width(u);
		}
	}
}

void update_cursor(struct window *w)
{
	unsigned int c = 8;

	update_cursor_x(w);
	update_cursor_y(w);
	if (w->cx - w->vx >= w->w)
		w->vx = (w->cx - w->w + c) & ~(c - 1);
	if (w->cx < w->vx)
		w->vx = w->cx / c * c;
	if (window->cy < window->vy)
		window->vy = window->cy;
	if (window->cy > window->vy + window->h - 1)
		window->vy = window->cy - window->h + 1;
}
