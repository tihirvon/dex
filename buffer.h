#ifndef BUFFER_H
#define BUFFER_H

#include "list.h"
#include "uchar.h"
#include "term.h"
#include "xmalloc.h"
#include "debug.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <limits.h>

#define BLOCK(item) container_of((item), struct block, node)
#define VIEW(item) container_of((item), struct view, node)
#define WINDOW(item) container_of((item), struct window, node)
#define BLOCK_SIZE 64

#define SET_CURSOR(bi) \
	do { \
		view->cursor.blk = bi.blk; \
		view->cursor.offset = bi.offset; \
	} while (0)

#define MIN_ALLOC 64U
#define ALLOC_ROUND(x) (((x) + MIN_ALLOC - 1) & ~(MIN_ALLOC - 1))

struct block {
	struct list_head node;
	unsigned int nl;
	unsigned int size;
	unsigned int alloc;
	char *data;
};

struct block_iter {
	struct list_head *head;
	struct block *blk;
	unsigned int offset;
};

struct change_head {
	struct change_head *next;
	struct change_head **prev;
	unsigned int nr_prev;
};

struct change {
	struct change_head head;
	unsigned int offset;
	unsigned count : 31;
	// after undoing backspace move after the text
	unsigned move_after : 1;
	// deleted bytes (inserted bytes need not to be saved)
	char *buf;
};

struct buffer {
	struct list_head blocks;
	struct change_head change_head;
	struct change_head *cur_change_head;

	// used to determine if buffer is modified
	struct change_head *save_change_head;

	struct stat st;

	unsigned int nl;

	char *filename;
	char *abs_filename;

	unsigned utf8 : 1;
	unsigned modified : 1;
	unsigned ro : 1;
	unsigned crlf : 1;

	int tab_width;

	unsigned int (*next_char)(struct block_iter *i, uchar *up);
	unsigned int (*prev_char)(struct block_iter *i, uchar *up);
	int (*get_char)(struct block_iter *i, uchar *up);
};

struct view {
	struct list_head node;
	struct buffer *buffer;
	struct window *window;

	struct block_iter cursor;

	// cursor y
	int cy;

	// cursor x (wide char 2, tab 1-8, control character 2, invalid char 4)
	int cx;

	// cursor x in characters (invalid utf8 character (byte) is one char)
	int cx_idx;

	// top left corner
	int vx, vy;

	// preferred cursor x (cx)
	int preferred_x;

	// Selection always starts at exact position of cursor and ends to
	// current position of cursor regardless of whether your are selecting
	// lines or not.
	struct block_iter sel;
	unsigned sel_is_lines : 1;
};

struct window {
	struct list_head node;
	struct list_head views;

	// current view. always exists
	struct view *view;

	int x, y;
	int w, h;
};

struct command {
	const char *name;
	const char *short_name;
	void (*cmd)(char **);
};

struct options {
	int move_wraps;
	int trim_whitespace;
	int auto_indent;
};

enum undo_merge {
	UNDO_MERGE_NONE,
	UNDO_MERGE_INSERT,
	UNDO_MERGE_DELETE,
	UNDO_MERGE_BACKSPACE
};

enum input_mode {
	INPUT_NORMAL,
	INPUT_COMMAND,
};

// from smallest update to largest. UPDATE_CURSOR_LINE includes
// UPDATE_STATUS_LINE and so on.
#define UPDATE_STATUS_LINE	(1 << 0)
#define UPDATE_CURSOR_LINE	(1 << 1)
#define UPDATE_FULL		(1 << 2)

// buffer = view->buffer = window->view->buffer
extern struct window *window;
extern struct view *view;
extern struct buffer *buffer;

extern struct list_head windows;

extern enum undo_merge undo_merge;
extern unsigned int update_flags;
extern struct command commands[];
extern int nr_pressed_keys;
extern int running;
extern enum input_mode input_mode;
extern struct options options;
extern char *home_dir;

static inline int buffer_modified(struct buffer *b)
{
	return b->save_change_head != b->cur_change_head;
}

struct block *block_new(int size);
void delete_block(struct block *blk);

unsigned int block_iter_next_byte(struct block_iter *i, uchar *byte);
unsigned int block_iter_prev_byte(struct block_iter *i, uchar *byte);
unsigned int block_iter_next_uchar(struct block_iter *i, uchar *up);
unsigned int block_iter_prev_uchar(struct block_iter *i, uchar *up);
unsigned int block_iter_next_line(struct block_iter *bi);
unsigned int block_iter_prev_line(struct block_iter *bi);
unsigned int block_iter_bol(struct block_iter *bi);
int block_iter_get_byte(struct block_iter *bi, uchar *up);
int block_iter_get_uchar(struct block_iter *bi, uchar *up);
unsigned int iter_get_offset(struct block_iter *bi);

unsigned int buffer_offset(void);
void record_change(unsigned int offset, char *buf, unsigned int len, int move_after);
void undo(void);
void redo(void);

char *path_absolute(const char *filename);
struct view *open_buffer(const char *filename);
void save_buffer(void);
char *buffer_get_bytes(unsigned int *lenp);

struct view *view_new(struct window *w, struct buffer *b);
void view_delete(struct view *v);

struct window *window_new(void);
struct view *window_add_buffer(struct buffer *b);
void remove_view(void);
void set_view(struct view *v);
void next_buffer(void);
void prev_buffer(void);
void update_cursor_x(struct view *v);
void update_cursor(struct view *v);

void update_preferred_x(void);
void do_insert(const char *buf, unsigned int len);
void do_delete(unsigned int len);
void delete(unsigned int len, int move_after);
void insert(const char *buf, unsigned int len);
void cut(unsigned int len, int is_lines);
void copy(unsigned int len, int is_lines);
unsigned int count_bytes_eol(struct block_iter *bi);
unsigned int prepare_selection(void);

void select_start(int is_lines);
void select_end(void);
void paste(void);
void delete_ch(void);
void backspace(void);
void insert_ch(unsigned int ch);

void move_left(int count);
void move_right(int count);
void move_bol(void);
void move_eol(void);
void move_up(int count);
void move_down(int count);
void move_bof(void);
void move_eof(void);

int buffer_get_char(uchar *up);

unsigned int count_nl(const char *buf, unsigned int size);
unsigned int copy_count_nl(char *dst, const char *src, unsigned int len);
ssize_t xread(int fd, void *buf, size_t count);
ssize_t xwrite(int fd, const void *buf, size_t count);

void read_config(void);
void ui_start(void);
void ui_end(void);

void handle_command(const char *cmd);
void handle_binding(enum term_key_type type, unsigned int key);

static inline int block_iter_eof(struct block_iter *bi)
{
	return bi->offset == bi->blk->size && bi->blk->node.next == bi->head;
}

#endif
