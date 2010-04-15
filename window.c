#include "window.h"
#include "file-history.h"

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
	struct view *v = xnew0(struct view, 1);

	b->ref++;
	v->buffer = b;
	v->window = window;
	list_add_before(&v->node, &window->views);
	return v;
}

void view_delete(struct view *v)
{
	struct buffer *b = v->buffer;

	if (v == prev_view)
		prev_view = NULL;

	if (!--b->ref) {
		if (b->options.file_history && b->abs_filename)
			add_file_history(v->cx_display, v->cy, b->abs_filename);
		free_buffer(b);
	}
	list_del(&v->node);
	free(v);
}

void remove_view(void)
{
	struct list_head *node = view->node.next;

	view_delete(view);
	view = NULL;

	if (prev_view) {
		set_view(prev_view);
		return;
	}
	if (list_empty(&window->views))
		open_empty_buffer();
	if (node == &window->views)
		node = node->prev;
	set_view(VIEW(node));
}

void set_view(struct view *v)
{
	if (view == v)
		return;

	/* forget previous view when changing view using any other command but open */
	prev_view = NULL;

	view = v;
	buffer = v->buffer;
	window = v->window;

	window->view = v;

	if (!buffer->setup)
		setup_buffer();

	update_flags |= UPDATE_FULL | UPDATE_TAB_BAR;
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

unsigned int count_nl(const char *buf, unsigned int size)
{
	unsigned int i, nl = 0;
	for (i = 0; i < size; i++) {
		if (buf[i] == '\n')
			nl++;
	}
	return nl;
}

void update_cursor_y(void)
{
	struct block *blk;
	unsigned int nl = 0;

	list_for_each_entry(blk, &buffer->blocks, node) {
		if (blk == view->cursor.blk) {
			nl += count_nl(blk->data, view->cursor.offset);
			view->cy = nl;
			return;
		}
		nl += blk->nl;
	}
	BUG_ON(1);
}

void update_cursor_x(void)
{
	unsigned int tw = buffer->options.tab_width;
	struct lineref lr;
	unsigned int idx = 0;

	view->cx = fetch_this_line(&view->cursor, &lr);
	view->cx_char = 0;
	view->cx_display = 0;
	while (idx < view->cx) {
		uchar u = u_buf_get_char(lr.line, lr.size, &idx);
		view->cx_char++;
		if (u == '\t') {
			view->cx_display = (view->cx_display + tw) / tw * tw;
		} else {
			view->cx_display += u_char_width(u);
		}
	}
}

static int update_view_y(void)
{
	if (view->cy < view->vy) {
		view->vy = view->cy;
		return 1;
	}
	if (view->cy > view->vy + window->h - 1) {
		view->vy = view->cy - window->h + 1;
		return 1;
	}
	return 0;
}

void update_cursor(void)
{
	unsigned int c = 8;

	update_cursor_x();
	update_cursor_y();
	if (view->cx_display - view->vx >= window->w)
		view->vx = (view->cx_display - window->w + c) & ~(c - 1);
	if (view->cx_display < view->vx)
		view->vx = view->cx_display / c * c;

	if (view->force_center || (update_view_y() && view->center_on_scroll))
		center_view_to_cursor();
	else {
		int margin = get_scroll_margin();
		int max_y;

		if (view->cy < view->vy + margin) {
			if (view->cy < margin)
				view->vy = 0;
			else
				view->vy = view->cy - margin;
		}
		max_y = view->vy + window->h - 1 - margin;
		if (view->cy > max_y) {
			view->vy += view->cy - max_y;
			max_y = buffer->nl - window->h + 1;
			if (view->vy > max_y && max_y >= 0)
				view->vy = max_y;
		}
	}

	view->force_center = 0;
	view->center_on_scroll = 0;
}

void center_view_to_cursor(void)
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
