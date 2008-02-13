#ifndef BUFFER_H
#define BUFFER_H

#include "list.h"
#include "uchar.h"
#include "term.h"
#include "xmalloc.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

#ifndef DEBUG
#define DEBUG 1
#endif

#if DEBUG <= 0
#define BUG(...) do { } while (0)
#define d_print(...) do { } while (0)
#else
#define BUG(...) bug(__FUNCTION__, __VA_ARGS__)
#define d_print(...) debug_print(__FUNCTION__, __VA_ARGS__)
#endif

#define __STR(a) #a
#define BUG_ON(a) \
	do { \
		if (unlikely(a)) \
			BUG("%s\n", __STR(a)); \
	} while (0)

#define BLOCK(item) container_of((item), struct block, node)
#define CHANGE(item) container_of((item), struct change, node)
#define BLOCK_SIZE 64

#define BLOCK_ITER_CURSOR(name, window) \
	struct block_iter name = { &window->buffer->blocks, window->cblk, window->coffset}

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
	unsigned int count;
	// deleted bytes (inserted bytes need not to be saved)
	char *buf;
};

struct buffer {
	struct list_head blocks;
	struct change_head change_head;
	struct change_head *cur_change_head;

	// used to determine if buffer is modified
	struct change_head *save_change_head;

	unsigned int nl;
	unsigned int size;

	char *filename;

	unsigned utf8 : 1;
	unsigned modified : 1;
	unsigned ro : 1;
	unsigned crlf : 1;

	int tab_width;

	int (*next_char)(struct block_iter *i, uchar *up);
	int (*prev_char)(struct block_iter *i, uchar *up);
	int (*get_char)(struct block_iter *i, uchar *up);
};

// There's no global list of buffers.  Each window has its own list of buffers
// although you can open same file to other window and they will share the
// buffer.  Currently there's only one fullscreen window.
struct window {
	struct buffer **buffers;
	struct buffer *buffer;
	int nr_buffers;

	// cursor
	struct block *cblk;
	unsigned int coffset;

	// cursor y
	int cy;

	// cursor x (wide char 2, tab 1-8, control character 2, invalid char 4)
	int cx;

	// cursor x in characters (invalid utf8 character (byte) is one char)
	int cx_idx;

	// top left corner
	int vx, vy;

	int x, y;
	int w, h;

	// preferred cursor x (cx)
	int preferred_x;

	// Selection always starts at exact position of cursor and ends to
	// current position of cursor regardless of whether your are selecting
	// lines or not.
	struct block *sel_blk;
	unsigned int sel_offset;
	unsigned sel_is_lines : 1;
};

struct command {
	const char *name;
	const char *short_name;
	void (*cmd)(char **);
};

enum undo_merge {
	UNDO_MERGE_NONE,
	UNDO_MERGE_INSERT,
	UNDO_MERGE_DELETE,
	UNDO_MERGE_BACKSPACE
};

// from smallest update to largest. UPDATE_CURSOR_LINE includes
// UPDATE_STATUS_LINE and so on.
#define UPDATE_STATUS_LINE	(1 << 0)
#define UPDATE_CURSOR_LINE	(1 << 1)
#define UPDATE_FULL		(1 << 2)

extern struct buffer *buffer;
extern struct window *window;
extern enum undo_merge undo_merge;
extern unsigned int update_flags;
extern struct command commands[];
extern struct binding *uncompleted_binding;
extern int running;

// options
extern int move_wraps;

static inline void init_block_iter_cursor(struct block_iter *bi, struct window *w)
{
	bi->head = &w->buffer->blocks;
	bi->blk = w->cblk;
	bi->offset = w->coffset;
}

struct block *block_new(int size);
void delete_block(struct block *blk);

int block_iter_next_byte(struct block_iter *i, uchar *byte);
int block_iter_prev_byte(struct block_iter *i, uchar *byte);
int block_iter_next_uchar(struct block_iter *i, uchar *up);
int block_iter_prev_uchar(struct block_iter *i, uchar *up);
int block_iter_next_line(struct block_iter *bi);
int block_iter_prev_line(struct block_iter *bi);
unsigned int block_iter_bol(struct block_iter *bi);
int block_iter_get_byte(struct block_iter *bi, uchar *up);
int block_iter_get_uchar(struct block_iter *bi, uchar *up);

unsigned int buffer_get_offset(struct block *stop_blk, unsigned int stop_offset);
unsigned int buffer_offset(void);
void record_change(unsigned int offset, char *buf, unsigned int len);
void undo(void);
void redo(void);

struct buffer *buffer_new(void);
struct buffer *buffer_load(const char *filename);
struct buffer *buffer_new_file(void);
void save_buffer(void);
char *buffer_get_bytes(unsigned int *lenp);

struct window *window_new(void);
void window_add_buffer(struct buffer *b);
void set_buffer(struct buffer *b);
void next_buffer(void);
void prev_buffer(void);
void update_cursor_x(struct window *w);
void update_cursor(struct window *w);

void update_preferred_x(void);
void do_insert(const char *buf, unsigned int len);
void do_delete(unsigned int len);
void insert(const char *buf, unsigned int len);
void cut(unsigned int len, int is_lines);
void copy(unsigned int len, int is_lines);
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

void bug(const char *function, const char *fmt, ...) __FORMAT(2, 3) __NORETURN;
void debug_print(const char *function, const char *fmt, ...) __FORMAT(2, 3);

#endif
