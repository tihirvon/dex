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

extern struct window *window;
extern struct list_head windows;
extern int nr_pressed_keys;
extern int running;

struct view *view_new(struct window *w, struct buffer *b);
void view_delete(struct view *v);

struct window *window_new(void);
struct view *window_add_buffer(struct buffer *b);

struct view *open_buffer(const char *filename);
int save_buffer(const char *filename);
void free_buffer(struct buffer *b);
void remove_view(void);
void set_view(struct view *v);
void next_buffer(void);
void prev_buffer(void);

void center_cursor(void);
void move_to_line(int line);

#endif
