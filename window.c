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
	struct view *v;

	BUG_ON(!b);
	BUG_ON(!window);

	v = view_new(window, b);
	list_add_before(&v->node, &window->views);
	return v;
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

static void restore_cursor_from_history(void)
{
	int x, y;

	if (!find_file_in_history(buffer->abs_filename, &x, &y))
		return;

	move_to_line(y + 1);
	move_to_column(x + 1);
}

void set_view(struct view *v)
{
	int restore_cursor = 0;

	/* on demand loading */
	while (list_empty(&v->buffer->blocks)) {
		if (!load_buffer(v->buffer, 0)) {
			v->cursor.head = &v->buffer->blocks;
			v->cursor.blk = BLOCK(v->buffer->blocks.next);
			restore_cursor = 1;
			break;
		}

		/* failed to read the file */
		view_delete(v);

		/* try to keep original view */
		if (view)
			return;

		if (list_empty(&window->views)) {
			v = open_empty_buffer();
			break;
		}
		v = VIEW(window->views.next);
	}

	if (view == v)
		return;

	/* forget previous view when changing view using any other command but open */
	prev_view = NULL;

	/*
	 * close untouched view opened by tag command
	 */
	if (view && view != v && view->temporary && !view->buffer->change_head.prev)
		view_delete(view);

	view = v;
	buffer = v->buffer;
	window = v->window;

	window->view = v;

	if (restore_cursor)
		restore_cursor_from_history();

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
	struct block_iter bi = view->cursor;
	unsigned int tw = buffer->options.tab_width;

	block_iter_bol(&bi);
	view->cx = 0;
	view->cx_char = 0;
	view->cx_display = 0;
	while (1) {
		uchar u;

		if (bi.blk == view->cursor.blk && bi.offset == view->cursor.offset)
			break;
		if (!view->cursor.offset && bi.offset == bi.blk->size && bi.blk->node.next == &view->cursor.blk->node) {
			// this[this.size] == this.next[0]
			break;
		}
		if (!buffer->next_char(&bi, &u))
			break;

		view->cx += buffer->utf8 ? u_char_size(u) : 1;
		view->cx_char++;
		if (u == '\t') {
			view->cx_display = (view->cx_display + tw) / tw * tw;
		} else {
			view->cx_display += u_char_width(u);
		}
	}
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

	if (view->cy < view->vy)
		view->vy = view->cy;
	if (view->cy > view->vy + window->h - 1)
		view->vy = view->cy - window->h + 1;
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
	update_cursor_y();
	if (view->cy > line)
		move_up(view->cy - line);
	if (view->cy < line)
		move_down(line - view->cy);
	center_cursor();
}
