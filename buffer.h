#ifndef BUFFER_H
#define BUFFER_H

#include "iter.h"
#include "list.h"
#include "uchar.h"
#include "options.h"
#include "common.h"

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

	// needed for identifying buffers whose filename is NULL
	unsigned int id;

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

enum selection {
	SELECT_NONE,
	SELECT_CHARS,
	SELECT_LINES,
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

	enum selection selection;

	// cursor offset when selection was started
	unsigned int sel_so;

	// If sel_eo is UINT_MAX that means the offset must be calculated from
	// the cursor iterator.  Otherwise the offset is precalculated and may
	// not be same as cursor position (see search/replace code).
	unsigned int sel_eo;

	// center view to cursor if scrolled
	unsigned center_on_scroll : 1;

	// force centering view to cursor
	unsigned force_center : 1;

	// used only when reading rc file
	unsigned rc_tmp : 1;
};

struct selection_info {
	struct block_iter si;
	unsigned int so;
	unsigned int eo;
	int swapped;
	int nr_lines;
	int nr_chars;
};

enum input_mode {
	INPUT_NORMAL,
	INPUT_COMMAND,
	INPUT_SEARCH,
};

#define UPDATE_STATUS_LINE	(1 << 0)
#define UPDATE_CURSOR_LINE	(1 << 1)
#define UPDATE_RANGE		(1 << 2)
#define UPDATE_FULL		(1 << 3)
#define UPDATE_TAB_BAR		(1 << 4)
#define UPDATE_COMMAND_LINE	(1 << 5)

// buffer = view->buffer = window->view->buffer
extern struct view *view;
extern struct buffer *buffer;
extern struct view *prev_view;

extern unsigned int update_flags;
extern enum input_mode input_mode;

static inline struct view *VIEW(struct list_head *item)
{
	return container_of(item, struct view, node);
}

static inline int buffer_modified(struct buffer *b)
{
	return b->save_change_head != b->cur_change_head;
}

static inline int selecting(void)
{
	return view->selection;
}

void init_selection(struct selection_info *info);
void fill_selection_info(struct selection_info *info);

void update_short_filename_cwd(struct buffer *b, const char *cwd);
void update_short_filename(struct buffer *b);
struct view *find_view_by_buffer_id(unsigned int buffer_id);
struct view *open_buffer(const char *filename, int must_exist);
struct view *open_empty_buffer(void);
void setup_buffer(void);
int save_buffer(const char *filename, enum newline_sequence newline);
void free_buffer(struct buffer *b);

char *buffer_get_bytes(unsigned int len);

void update_preferred_x(void);
void insert(const char *buf, unsigned int len);
void replace(unsigned int del_count, const char *inserted, int ins_count);
unsigned int count_bytes_eol(struct block_iter *bi);

void delete_ch(void);
void erase(void);
void insert_ch(unsigned int ch);

unsigned int buffer_get_char(struct block_iter *bi, uchar *up);
unsigned int buffer_next_char(struct block_iter *bi, uchar *up);
unsigned int buffer_prev_char(struct block_iter *bi, uchar *up);

static inline void buffer_bof(struct block_iter *bi)
{
	bi->head = &buffer->blocks;
	bi->blk = BLOCK(buffer->blocks.next);
	bi->offset = 0;
}

static inline void buffer_eof(struct block_iter *bi)
{
	bi->head = &buffer->blocks;
	bi->blk = BLOCK(buffer->blocks.prev);
	bi->offset = bi->blk->size;
}

char *get_word_under_cursor(void);

int guess_filetype(void);
struct syntax *load_syntax(const char *filetype);
void syntax_changed(void);
void filetype_changed(void);

#endif
