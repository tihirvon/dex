#include "window.h"
#include "view.h"
#include "file-history.h"

PTR_ARRAY(windows);
struct window *window;

static struct view *prev_view;

struct window *new_window(void)
{
	return xnew0(struct window, 1);
}

struct view *window_add_buffer(struct window *w, struct buffer *b)
{
	struct view *v = xnew0(struct view, 1);

	v->buffer = b;
	v->window = w;
	v->cursor.head = &b->blocks;
	v->cursor.blk = BLOCK(b->blocks.next);

	ptr_array_add(&b->views, v);
	ptr_array_add(&w->views, v);
	w->update_tabbar = true;
	return v;
}

struct view *window_open_empty_buffer(struct window *w)
{
	return window_add_buffer(w, open_empty_buffer());
}

struct view *window_get_view(struct window *w, struct buffer *b)
{
	struct view *v;
	int i;

	for (i = 0; i < b->views.count; i++) {
		v = b->views.ptrs[i];
		if (v->window == w) {
			// already open in this window
			return v;
		}
	}
	// open the buffer in other window to this window
	v = window_add_buffer(w, b);
	v->cursor = ((struct view *)b->views.ptrs[0])->cursor;
	return v;
}

struct view *window_find_unclosable_view(struct window *w, bool (*can_close)(struct view *))
{
	long i;

	// check active view first
	if (w->view != NULL && !can_close(w->view)) {
		return w->view;
	}
	for (i = 0; i < w->views.count; i++) {
		struct view *v = w->views.ptrs[i];
		if (!can_close(v)) {
			return v;
		}
	}
	return NULL;
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
	long idx = ptr_array_idx(&window->views, view);

	remove_view(view);
	if (prev_view) {
		set_view(prev_view);
		return;
	}
	if (window->views.count == 0)
		window_open_empty_buffer(window);
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
	struct view *v = window_open_empty_buffer(window);

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
static bool is_useless_empty_view(struct view *v)
{
	if (v == NULL)
		return false;
	if (v->window->views.count != 1)
		return false;
	// touched?
	if (v->buffer->abs_filename != NULL || v->buffer->change_head.nr_prev != 0)
		return false;
	return true;
}

struct view *open_file(const char *filename, const char *encoding)
{
	struct view *prev = view;
	bool useless = is_useless_empty_view(prev);
	struct view *v = open_buffer(filename, false, encoding);

	if (v == NULL)
		return NULL;

	set_view(v);
	if (useless) {
		remove_view(prev);
	} else {
		prev_view = prev;
	}
	return v;
}

void open_files(char **filenames, const char *encoding)
{
	struct view *empty = view;
	bool useless = is_useless_empty_view(empty);
	bool first = true;
	int i;

	for (i = 0; filenames[i]; i++) {
		struct view *v = open_buffer(filenames[i], false, encoding);
		if (v && first) {
			set_view(v);
			first = false;
		}
	}
	if (useless && view != empty) {
		remove_view(empty);
	}
}

void mark_buffer_tabbars_changed(void)
{
	long i;
	for (i = 0; i < buffer->views.count; i++) {
		struct view *v = buffer->views.ptrs[i];
		v->window->update_tabbar = true;
	}
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
		mark_all_lines_changed(win->view->buffer);
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

int window_get_scroll_margin(struct window *w)
{
	int max = (w->edit_h - 1) / 2;

	if (options.scroll_margin > max)
		return max;
	return options.scroll_margin;
}
