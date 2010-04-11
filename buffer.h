#ifndef BUFFER_H
#define BUFFER_H

#include "common.h"
#include "iter.h"
#include "list.h"
#include "uchar.h"
#include "options.h"

struct change_head {
	struct change_head *next;
	struct change_head **prev;
	unsigned int nr_prev;
};

struct buffer {
	struct list_head blocks;
	struct change_head change_head;
	struct change_head *cur_change_head;

	// used to determine if buffer is modified
	struct change_head *save_change_head;

	struct stat st;

	unsigned int nl;

	// number of views pointing to this buffer
	int ref;

	char *filename;
	char *abs_filename;

	unsigned utf8 : 1;
	unsigned ro : 1;
	unsigned locked : 1;
	unsigned setup : 1;

	enum newline_sequence newline;

	struct local_options options;

	const struct syntax *syn;
	struct list_head hl_head;
};

struct view {
	struct list_head node;
	struct buffer *buffer;
	struct window *window;

	struct block_iter cursor;

	// cursor position
	int cx, cy;

	// visual cursor x
	// character widths: wide 2, tab 1-8, control 2, invalid char 4
	int cx_display;

	// cursor x in characters (invalid utf8 character (byte) is one char)
	int cx_char;

	// top left corner
	int vx, vy;

	// preferred cursor x (preferred value for cx_display)
	int preferred_x;

	// tab title
	int tt_width;
	int tt_truncated_width;

	// Selection always starts at exact position of cursor and ends to
	// current position of cursor regardless of whether your are selecting
	// lines or not.
	struct block_iter sel;
	unsigned sel_is_lines : 1;

	// center view to cursor if scrolled
	unsigned center_on_scroll : 1;

	// force centering view to cursor
	unsigned force_center : 1;

	// used only when reading rc file
	unsigned rc_tmp : 1;
};

struct selection_info {
	struct block_iter si;
	struct block_iter ei;
	unsigned int so;
	unsigned int eo;
	int swapped;
	int nr_lines;
	int nr_chars;
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
	INPUT_SEARCH,
};

enum input_special {
	/* not inputting special characters */
	INPUT_SPECIAL_NONE,

	/* not known yet (just started by hitting ^V) */
	INPUT_SPECIAL_UNKNOWN,

	/* accept any value 0-255 (3 octal digits) */
	INPUT_SPECIAL_OCT,

	/* accept any value 0-255 (3 decimal digits) */
	INPUT_SPECIAL_DEC,

	/* accept any value 0-255 (2 hexadecimal digits) */
	INPUT_SPECIAL_HEX,

	/* accept any valid unicode value (6 hexadecimal digits) */
	INPUT_SPECIAL_UNICODE,
};

#define UPDATE_STATUS_LINE	(1 << 0)
#define UPDATE_CURSOR_LINE	(1 << 1)
#define UPDATE_RANGE		(1 << 2)
#define UPDATE_FULL		(1 << 3)
#define UPDATE_TAB_BAR		(1 << 4)

// buffer = view->buffer = window->view->buffer
extern struct view *view;
extern struct buffer *buffer;
extern struct view *prev_view;

extern enum undo_merge undo_merge;
extern unsigned int update_flags;
extern enum input_mode input_mode;
extern enum input_special input_special;

static inline struct view *VIEW(struct list_head *item)
{
	return container_of(item, struct view, node);
}

static inline int buffer_modified(struct buffer *b)
{
	return b->save_change_head != b->cur_change_head;
}

void init_selection(struct selection_info *info);
void fill_selection_info(struct selection_info *info);

struct view *open_buffer(const char *filename, int must_exist);
struct view *open_empty_buffer(void);
void setup_buffer(void);
int save_buffer(const char *filename, enum newline_sequence newline);
void free_buffer(struct buffer *b);

void move_to_offset(unsigned int offset);

char *buffer_get_bytes(unsigned int len);

void update_cursor_x(void);
void update_cursor_y(void);
void update_cursor(void);

void update_preferred_x(void);
void move_to_preferred_x(void);
void do_insert(const char *buf, unsigned int len);
char *do_delete(unsigned int len);
void insert(const char *buf, unsigned int len);
void replace(unsigned int del_count, const char *inserted, int ins_count);
unsigned int count_bytes_eol(struct block_iter *bi);

void delete_ch(void);
void erase(void);
void insert_ch(unsigned int ch);

void move_up(int count);
void move_down(int count);
void move_bof(void);
void move_eof(void);

unsigned int buffer_get_char(struct block_iter *bi, uchar *up);
unsigned int buffer_next_char(struct block_iter *bi, uchar *up);
unsigned int buffer_prev_char(struct block_iter *bi, uchar *up);

char *get_word_under_cursor(void);

int guess_filetype(void);
struct syntax *load_syntax(const char *filetype);
void syntax_changed(void);
void filetype_changed(void);

#endif
