#include "window.h"

LIST_HEAD(windows);
struct window *window;

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
	struct list_head *prev = view->node.prev;

	view_delete(view);
	if (list_empty(&window->views))
		open_empty_buffer();
	if (prev == &window->views)
		prev = prev->next;
	set_view(VIEW(prev));
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
	unsigned int tw = v->buffer->options.tab_width;

	block_iter_bol(&bi);
	v->cx = 0;
	v->cx_char = 0;
	v->cx_display = 0;
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

		v->cx += buffer->utf8 ? u_char_size(u) : 1;
		v->cx_char++;
		if (u == '\t') {
			v->cx_display = (v->cx_display + tw) / tw * tw;
		} else {
			v->cx_display += u_char_width(u);
		}
	}
}

void update_cursor(struct view *v)
{
	unsigned int c = 8;

	update_cursor_x(v);
	update_cursor_y(v);
	if (v->cx_display - v->vx >= v->window->w)
		v->vx = (v->cx_display - v->window->w + c) & ~(c - 1);
	if (v->cx_display < v->vx)
		v->vx = v->cx_display / c * c;

	if (v->cy < v->vy)
		v->vy = v->cy;
	if (v->cy > v->vy + v->window->h - 1)
		v->vy = v->cy - v->window->h + 1;
}

void center_cursor(void)
{
	unsigned int hh = window->h / 2;

	if (window->h >= buffer->nl || view->cy < hh) {
		view->vy = 0;
		return;
	}

	view->vy = view->cy - hh;
	if (view->vy + window->h > buffer->nl) {
		/* -1 makes one ~ line visible so that you know where the EOF is */
		view->vy -= view->vy + window->h - buffer->nl - 1;
	}
}

void move_to_line(int line)
{
	line--;
	update_cursor(view);
	if (view->cy > line)
		move_up(view->cy - line);
	if (view->cy < line)
		move_down(line - view->cy);
	center_cursor();
}
