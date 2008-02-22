#include "buffer.h"

struct window *window_new(void)
{
	struct window *w = xnew0(struct window, 1);

	list_init(&w->views);
	list_add_before(&w->node, &windows);
	w->w = 80;
	w->h = 24;
	return w;
}

struct view *window_add_buffer(struct buffer *b)
{
	struct view *v;

	BUG_ON(!b);
	BUG_ON(!window);

	// FIXME: don't allow multiple buffers (views) on same window
	v = view_new(window, b);
	list_add_before(&v->node, &window->views);
	return v;
}

void remove_view(void)
{
	struct list_head *next = view->node.next;

	view_delete(view);
	if (list_empty(&window->views))
		open_buffer(NULL);
	if (next == &window->views)
		next = next->next;
	set_view(VIEW(next));
}

void set_view(struct view *v)
{
	view = v;
	buffer = v->buffer;
	window = v->window;

	window->view = v;

	update_flags |= UPDATE_FULL;
}

void next_buffer(void)
{
	BUG_ON(!window);
	BUG_ON(!window->view);
	if (window->view->node.next == &window->views) {
		set_view(VIEW(window->views.next));
	} else {
		set_view(VIEW(window->view->node.next));
	}
}

void prev_buffer(void)
{
	if (window->view->node.prev == &window->views) {
		set_view(VIEW(window->views.prev));
	} else {
		set_view(VIEW(window->view->node.prev));
	}
}

static void update_cursor_y(struct view *v)
{
	struct block *blk;
	unsigned int nl = 0;

	list_for_each_entry(blk, &v->buffer->blocks, node) {
		if (blk == v->cursor.blk) {
			nl += count_nl(blk->data, v->cursor.offset);
			v->cy = nl;
			return;
		}
		nl += blk->nl;
	}
	BUG_ON(1);
}

void update_cursor_x(struct view *v)
{
	struct block_iter bi = v->cursor;
	unsigned int tw = v->buffer->tab_width;

	block_iter_bol(&bi);
	v->cx = 0;
	v->cx_idx = 0;
	while (1) {
		uchar u;

		if (bi.blk == v->cursor.blk && bi.offset == v->cursor.offset)
			break;
		if (!v->cursor.offset && bi.offset == bi.blk->size && bi.blk->node.next == &v->cursor.blk->node) {
			// this[this.size] == this.next[0]
			break;
		}
		if (!v->buffer->next_char(&bi, &u))
			break;
		v->cx_idx++;
		if (u == '\t') {
			v->cx = (v->cx + tw) / tw * tw;
		} else if (u < 0x20) {
			v->cx += 2;
		} else {
			v->cx += u_char_width(u);
		}
	}
}

void update_cursor(struct view *v)
{
	unsigned int c = 8;

	update_cursor_x(v);
	update_cursor_y(v);
	if (v->cx - v->vx >= v->window->w)
		v->vx = (v->cx - v->window->w + c) & ~(c - 1);
	if (v->cx < v->vx)
		v->vx = v->cx / c * c;

	if (v->cy < v->vy)
		v->vy = v->cy;
	if (v->cy > v->vy + v->window->h - 1)
		v->vy = v->cy - v->window->h + 1;
}
