#ifndef BUFFER_H
#define BUFFER_H

#include "iter.h"
#include "list.h"
#include "options.h"
#include "common.h"
#include "ptr-array.h"

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

	// struct stat is 144 bytes on x86-64
	off_t st_size;
	dev_t st_dev;
	ino_t st_ino;
	time_t _st_mtime; // st_mtime is a macro in GarbageLIBC
	mode_t st_mode;

	// needed for identifying buffers whose filename is NULL
	unsigned int id;

	unsigned int nl;

	// number of views pointing to this buffer
	int ref;

	char *filename;
	char *abs_filename;

	unsigned ro : 1;
	unsigned locked : 1;
	unsigned setup : 1;

	enum newline_sequence newline;

	struct local_options options;

	const struct syntax *syn;
	// Index 0 is always syn->states.ptrs[0].
	// Lowest bit of an invalidated value is 1.
	struct ptr_array line_start_states;
};

enum selection {
	SELECT_NONE,
	SELECT_CHARS,
	SELECT_LINES,
};

struct view {
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
};

struct selection_info {
	struct block_iter si;
	unsigned int so;
	unsigned int eo;
	int swapped;
	int nr_lines;
	int nr_chars;
};

#define UPDATE_TAB_BAR		(1 << 0)
#define UPDATE_FULL		(1 << 1)
#define UPDATE_COMMAND_LINE	(1 << 2)
#define UPDATE_WINDOW_SIZES	(1 << 3)

// buffer = view->buffer = window->view->buffer
extern struct view *view;
extern struct buffer *buffer;
extern struct view *prev_view;

extern unsigned int update_flags;
extern int changed_line_min;
extern int changed_line_max;

static inline int buffer_modified(struct buffer *b)
{
	return b->save_change_head != b->cur_change_head;
}

static inline int selecting(void)
{
	return view->selection;
}

void lines_changed(int min, int max);
unsigned int count_nl(const char *buf, unsigned int size);

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

unsigned int buffer_get_char(struct block_iter *bi, unsigned int *up);
unsigned int buffer_next_char(struct block_iter *bi, unsigned int *up);
unsigned int buffer_prev_char(struct block_iter *bi, unsigned int *up);

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
struct syntax *load_syntax_by_filename(const char *filename);
struct syntax *load_syntax_by_filetype(const char *filetype);
void syntax_changed(void);
void filetype_changed(void);

void update_cursor_y(void);
void update_cursor_x(void);
void update_preferred_x(void);

#endif
