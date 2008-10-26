#ifndef WINDOW_H
#define WINDOW_H

#include "buffer.h"

#define WINDOW(item) container_of((item), struct window, node)

struct window {
	struct list_head node;
	struct list_head views;

	// current view. always exists
	struct view *view;

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

struct view *view_new(struct window *w, struct buffer *b);
void view_init(struct view *v);
void view_delete(struct view *v);

struct window *window_new(void);
struct view *window_add_buffer(struct buffer *b);

#define OF_LOAD_BUFFER		0x01
#define OF_FILE_MUST_EXIST	0x02
#define OF_TEMPORARY		0x04

struct view *open_buffer(const char *filename, unsigned int flags);
struct view *open_empty_buffer(void);
int load_buffer(struct buffer *b, int must_exist);

int save_buffer(const char *filename, enum newline_sequence newline);
void free_buffer(struct buffer *b);
void remove_view(void);
void set_view(struct view *v);
void next_buffer(void);
void prev_buffer(void);

void center_cursor(void);
void move_to_line(int line);

static inline void move_to_column(int column)
{
	view->preferred_x = column - 1;
	move_preferred_x();
}

#endif
