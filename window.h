#ifndef WINDOW_H
#define WINDOW_H

#include "buffer.h"

struct window {
	struct list_head node;
	struct list_head views;

	/* current view. always exists */
	struct view *view;

	/* Coordinates and size of editable area not including tabs,
	 * status line and command line.
	 */
	int x, y;
	int w, h;
};

enum editor_status {
	EDITOR_INITIALIZING,
	EDITOR_RUNNING,
	EDITOR_EXITING,
};

extern struct window *window;
extern struct list_head windows;
extern int nr_pressed_keys;
extern enum editor_status editor_status;

static inline struct window *WINDOW(struct list_head *item)
{
	return container_of(item, struct window, node);
}

struct view *view_new(struct window *w, struct buffer *b);
void view_delete(struct view *v);

struct window *window_new(void);
struct view *window_add_buffer(struct buffer *b);

struct view *open_buffer(const char *filename, int must_exist);
struct view *open_empty_buffer(void);
void setup_buffer(void);

int save_buffer(const char *filename, enum newline_sequence newline);
void free_buffer(struct buffer *b);
void remove_view(void);
void set_view(struct view *v);
void next_buffer(void);
void prev_buffer(void);

void center_view_to_cursor(void);
void move_to_line(int line);

static inline void move_to_column(int column)
{
	view->preferred_x = column - 1;
	move_preferred_x();
}

#endif
