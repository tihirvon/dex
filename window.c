#include "window.h"
#include "file-history.h"

PTR_ARRAY(windows);
struct window *window;

static struct view *prev_view;

struct window *new_window(void)
{
	return xnew0(struct window, 1);
}

struct view *window_add_buffer(struct buffer *b)
{
	struct view *v = xnew0(struct view, 1);

	v->buffer = b;
	v->window = window;
	v->cursor.head = &b->blocks;
	v->cursor.blk = BLOCK(b->blocks.next);

	ptr_array_add(&b->views, v);
	ptr_array_add(&window->views, v);
	window->update_tabbar = true;
	return v;
}

void window_remove_views(struct window *w)
{
	while (w->views.count > 0) {
		struct view *v = w->views.ptrs[w->views.count - 1];
		remove_view(v);
	}
}

// NOTE: w->frame isn't removed
void window_free(struct window *w)
{
	window_remove_views(w);
	free(w->views.ptrs);
	w->frame = NULL;
	free(w);
}

// Remove view from v->window and v->buffer->views and free it.
void remove_view(struct view *v)
{
	struct buffer *b = v->buffer;

	// stupid globals
	if (v == prev_view) {
		prev_view = NULL;
	}
	if (v == view) {
		view = NULL;
		buffer = NULL;
	}

	ptr_array_remove(&v->window->views, v);
	v->window->update_tabbar = true;

	ptr_array_remove(&b->views, v);
	if (b->views.count == 0) {
		if (b->options.file_history && b->abs_filename)
			add_file_history(v->cy + 1, v->cx_char + 1, b->abs_filename);
		free_buffer(b);
	}
	free(v);
}

void close_current_view(void)
{
	int idx = view_idx();

	remove_view(view);
	if (prev_view) {
		set_view(prev_view);
		return;
	}
	if (window->views.count == 0)
		open_empty_buffer();
	if (window->views.count == idx)
		idx--;
	set_view(window->views.ptrs[idx]);
}

void set_view(struct view *v)
{
	int i;

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

	// view.cursor can be invalid if same buffer was modified from another view
	if (view->restore_cursor) {
		view->cursor.blk = BLOCK(view->buffer->blocks.next);
		block_iter_goto_offset(&view->cursor, view->saved_cursor_offset);
		view->restore_cursor = false;
		view->saved_cursor_offset = 0;
	}

	// save cursor states of views sharing same buffer
	for (i = 0; i < buffer->views.count; i++) {
		v = buffer->views.ptrs[i];
		if (v != view) {
			v->saved_cursor_offset = block_iter_get_offset(&v->cursor);
			v->restore_cursor = true;
		}
	}
}

struct view *open_new_file(void)
{
	struct view *prev = view;
	struct view *v = open_empty_buffer();

	set_view(v);
	prev_view = prev;
	return v;
}

/*
If window contains only one buffer and it is untouched then it will be
closed after opening another file. This is done because closing last
buffer causes an empty buffer to be opened (window must contain at least
one buffer).
*/
static struct view *useless_empty_view(void)
{
	if (window->views.count != 1)
		return NULL;
	// touched?
	if (buffer->abs_filename != NULL || buffer->change_head.nr_prev != 0)
		return NULL;
	return view;
}

struct view *open_file(const char *filename, const char *encoding)
{
	struct view *empty = useless_empty_view();
	struct view *prev = view;
	struct view *v = open_buffer(filename, false, encoding);

	if (v == NULL)
		return NULL;

	set_view(v);
	if (empty != NULL) {
		remove_view(empty);
	} else {
		prev_view = prev;
	}
	return v;
}

void open_files(char **filenames, const char *encoding)
{
	struct view *empty = useless_empty_view();
	bool first = true;
	int i;

	for (i = 0; filenames[i]; i++) {
		struct view *v = open_buffer(filenames[i], false, encoding);
		if (v && first) {
			set_view(v);
			first = false;
		}
	}
	if (empty != NULL && view != empty) {
		remove_view(empty);
	}
}

static int cursor_outside_view(void)
{
	return view->cy < view->vy || view->cy > view->vy + window->edit_h - 1;
}

static void center_view_to_cursor(void)
{
	unsigned int hh = window->edit_h / 2;

	if (window->edit_h >= buffer->nl || view->cy < hh) {
		view->vy = 0;
		return;
	}

	view->vy = view->cy - hh;
	if (view->vy + window->edit_h > buffer->nl) {
		/* -1 makes one ~ line visible so that you know where the EOF is */
		view->vy -= view->vy + window->edit_h - buffer->nl - 1;
	}
}

static void update_view_x(void)
{
	unsigned int c = 8;

	if (view->cx_display - view->vx >= window->edit_w)
		view->vx = (view->cx_display - window->edit_w + c) / c * c;
	if (view->cx_display < view->vx)
		view->vx = view->cx_display / c * c;
}

static void update_view_y(void)
{
	int margin = get_scroll_margin();
	int max_y = view->vy + window->edit_h - 1 - margin;

	if (view->cy < view->vy + margin) {
		view->vy = view->cy - margin;
		if (view->vy < 0)
			view->vy = 0;
	} else if (view->cy > max_y) {
		view->vy += view->cy - max_y;
		max_y = buffer->nl - window->edit_h + 1;
		if (view->vy > max_y && max_y >= 0)
			view->vy = max_y;
	}
}

void update_view(void)
{
	update_view_x();
	if (view->force_center || (view->center_on_scroll && cursor_outside_view()))
		center_view_to_cursor();
	else
		update_view_y();

	view->force_center = false;
	view->center_on_scroll = false;
}

int vertical_tabbar_width(struct window *win)
{
	// line numbers are included in min_edit_w
	int min_edit_w = 80;
	int w = 0;

	if (options.show_tab_bar && options.vertical_tab_bar)
		w = options.tab_bar_width;
	if (win->w - w < min_edit_w)
		w = win->w - min_edit_w;
	if (w < TAB_BAR_MIN_WIDTH)
		w = 0;
	return w;
}

static int line_numbers_width(struct window *win)
{
	int w = 0, min_w = 5;

	if (options.show_line_numbers && win->view) {
		w = number_width(win->view->buffer->nl) + 1;
		if (w < min_w)
			w = min_w;
	}
	return w;
}

static int edit_x_offset(struct window *win)
{
	return line_numbers_width(win) + vertical_tabbar_width(win);
}

static int edit_y_offset(struct window *win)
{
	if (options.show_tab_bar && !options.vertical_tab_bar)
		return 1;
	return 0;
}

static void set_edit_size(struct window *win)
{
	int xo = edit_x_offset(win);
	int yo = edit_y_offset(win);

	win->edit_w = win->w - xo;
	win->edit_h = win->h - yo - 1; // statusline
	win->edit_x = win->x + xo;
}

void calculate_line_numbers(struct window *win)
{
	int w = line_numbers_width(win);

	if (w != win->line_numbers.width) {
		win->line_numbers.width = w;
		win->line_numbers.first = 0;
		win->line_numbers.last = 0;
		mark_all_lines_changed();
	}
	set_edit_size(win);
}

void set_window_coordinates(struct window *win, int x, int y)
{
	win->x = x;
	win->y = y;
	win->edit_x = x + edit_x_offset(win);
	win->edit_y = y + edit_y_offset(win);
}

void set_window_size(struct window *win, int w, int h)
{
	win->w = w;
	win->h = h;
	calculate_line_numbers(win);
}
