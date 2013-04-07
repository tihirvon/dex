#ifndef BUFFER_H
#define BUFFER_H

#include "iter.h"
#include "list.h"
#include "options.h"
#include "common.h"
#include "ptr-array.h"

struct change {
	struct change *next;
	struct change **prev;
	unsigned int nr_prev;

	// move after inserted text when undoing delete?
	bool move_after;

	long offset;
	long del_count;
	long ins_count;

	// deleted bytes (inserted bytes need not to be saved)
	char *buf;
};

struct buffer {
	struct list_head blocks;
	struct change change_head;
	struct change *cur_change;

	// used to determine if buffer is modified
	struct change *saved_change;

	struct stat st;

	// needed for identifying buffers whose filename is NULL
	unsigned int id;

	long nl;

	// views pointing to this buffer
	struct ptr_array views;

	char *display_filename;
	char *abs_filename;

	bool ro;
	bool locked;
	bool setup;

	enum newline_sequence newline;

	// Encoding of the file. Buffer always contains UTF-8.
	char *encoding;

	struct local_options options;

	struct syntax *syn;
	// Index 0 is always syn->states.ptrs[0].
	// Lowest bit of an invalidated value is 1.
	struct ptr_array line_start_states;

	int changed_line_min;
	int changed_line_max;
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
	long sel_so;

	// If sel_eo is UINT_MAX that means the offset must be calculated from
	// the cursor iterator.  Otherwise the offset is precalculated and may
	// not be same as cursor position (see search/replace code).
	long sel_eo;

	// center view to cursor if scrolled
	bool center_on_scroll;

	// force centering view to cursor
	bool force_center;

	// These are used to save cursor state when there are multiple views
	// sharing same buffer.
	bool restore_cursor;
	long saved_cursor_offset;
};

// buffer = view->buffer = window->view->buffer
extern struct view *view;
extern struct buffer *buffer;
extern struct ptr_array buffers;
extern bool everything_changed;

static inline void mark_all_lines_changed(void)
{
	buffer->changed_line_min = 0;
	buffer->changed_line_max = INT_MAX;
}

static inline void mark_everything_changed(void)
{
	everything_changed = true;
}

static inline bool buffer_modified(struct buffer *b)
{
	return b->saved_change != b->cur_change;
}

void lines_changed(int min, int max);
const char *buffer_filename(struct buffer *b);
long count_nl(const char *buf, long size);
char *buffer_get_bytes(long len);
char *get_selection(long *size);
char *get_word_under_cursor(void);

void update_short_filename_cwd(struct buffer *b, const char *cwd);
void update_short_filename(struct buffer *b);
struct view *buffer_get_view(struct buffer *b);
struct buffer *find_buffer_by_id(unsigned int id);
struct view *open_buffer(const char *filename, bool must_exist, const char *encoding);
struct view *open_empty_buffer(void);
void setup_buffer(void);
void free_buffer(struct buffer *b);

long buffer_get_char(struct block_iter *bi, unsigned int *up);
long buffer_next_char(struct block_iter *bi, unsigned int *up);
long buffer_prev_char(struct block_iter *bi, unsigned int *up);

bool guess_filetype(void);
void syntax_changed(void);
void filetype_changed(void);

static inline void view_reset_preferred_x(struct view *v)
{
	v->preferred_x = -1;
}

#endif
