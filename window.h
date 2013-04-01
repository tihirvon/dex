#ifndef WINDOW_H
#define WINDOW_H

#include "buffer.h"

struct window {
	struct ptr_array views;
	struct frame *frame;

	// Current view
	struct view *view;

	// Coordinates and size of entire window including tabbar and status line
	int x, y;
	int w, h;

	// Coordinates and size of editable area
	int edit_x, edit_y;
	int edit_w, edit_h;

	struct {
		int width;
		int first;
		int last;
	} line_numbers;

	int first_tab_idx;

	bool update_tabbar;
};

extern struct window *window;
extern struct ptr_array windows;

static inline struct window *WINDOW(int i)
{
	return windows.ptrs[i];
}

static inline void mark_buffer_tabbars_changed(void)
{
	int i;
	for (i = 0; i < buffer->views.count; i++) {
		struct view *v = buffer->views.ptrs[i];
		v->window->update_tabbar = true;
	}
}

struct window *window_new(void);
struct view *window_add_buffer(struct buffer *b);
void view_delete(struct view *v);
void close_current_view(void);
void set_view(struct view *v);
struct view *open_new_file(void);
struct view *open_file(const char *filename, const char *encoding);
void open_files(char **filenames, const char *encoding);
void update_view(void);
int vertical_tabbar_width(struct window *win);
void calculate_line_numbers(struct window *win);
void set_window_coordinates(struct window *win, int x, int y);
void set_window_size(struct window *win, int w, int h);

static inline int window_idx(void)
{
	return ptr_array_idx(&windows, window);
}

static inline int view_idx(void)
{
	return ptr_array_idx(&window->views, view);
}

static inline int new_window_idx(int idx)
{
	return (idx + windows.count) % windows.count;
}

static inline int new_view_idx(int idx)
{
	return (idx + window->views.count) % window->views.count;
}

static inline int get_scroll_margin(void)
{
	int max = (window->edit_h - 1) / 2;

	if (options.scroll_margin > max)
		return max;
	return options.scroll_margin;
}

#endif
