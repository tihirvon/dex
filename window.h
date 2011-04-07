#ifndef WINDOW_H
#define WINDOW_H

#include "buffer.h"

struct window {
	struct ptr_array views;

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
};

extern struct window *window;
extern struct ptr_array windows;

static inline struct window *WINDOW(int i)
{
	return windows.ptrs[i];
}

static inline struct view *VIEW(int i, int j)
{
	return WINDOW(i)->views.ptrs[j];
}

struct window *window_new(void);
struct view *window_add_buffer(struct buffer *b);
void view_delete(struct view *v);
void remove_view(void);
void set_view(struct view *v);
void update_view(void);
int view_idx(void);
void calculate_line_numbers(struct window *win);
void set_window_coordinates(struct window *win, int x, int y);
void set_window_size(struct window *win, int w, int h);

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
