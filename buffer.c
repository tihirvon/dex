#include "load-save.h"
#include "buffer.h"
#include "window.h"
#include "editor.h"
#include "error.h"
#include "change.h"
#include "block.h"
#include "move.h"
#include "filetype.h"
#include "state.h"
#include "syntax.h"
#include "file-history.h"
#include "file-option.h"
#include "lock.h"
#include "selection.h"
#include "path.h"
#include "unicode.h"
#include "uchar.h"
#include "detect.h"

struct buffer *buffer;
PTR_ARRAY(buffers);
bool everything_changed;

static void set_display_filename(struct buffer *b, char *name)
{
	free(b->display_filename);
	b->display_filename = name;
}

/*
 * Mark line range min...max (inclusive) "changed". These lines will be
 * redrawn when screen is updated. This is called even when content has not
 * been changed, but selection has or line has been deleted and all lines
 * after the deleted line move up.
 *
 * Syntax highlighter has different logic. It cares about contents of the
 * lines, not about selection or if the lines have been moved up or down.
 */
void lines_changed(int min, int max)
{
	if (min > max) {
		int tmp = min;
		min = max;
		max = tmp;
	}

	if (min < buffer->changed_line_min)
		buffer->changed_line_min = min;
	if (max > buffer->changed_line_max)
		buffer->changed_line_max = max;
}

const char *buffer_filename(struct buffer *b)
{
	return b->display_filename;
}

long count_nl(const char *buf, long size)
{
	const char *end = buf + size;
	long nl = 0;

	while (buf < end) {
		buf = memchr(buf, '\n', end - buf);
		if (!buf)
			break;
		buf++;
		nl++;
	}
	return nl;
}

char *buffer_get_bytes(long len)
{
	struct block *blk = view->cursor.blk;
	long offset = view->cursor.offset;
	long pos = 0;
	char *buf;

	if (!len)
		return NULL;

	buf = xnew(char, len);
	while (pos < len) {
		long avail = blk->size - offset;
		long count = len - pos;

		if (count > avail)
			count = avail;
		memcpy(buf + pos, blk->data + offset, count);
		pos += count;

		BUG_ON(pos < len && blk->node.next == &buffer->blocks);
		blk = BLOCK(blk->node.next);
		offset = 0;
	}
	return buf;
}

char *get_selection(long *size)
{
	struct block_iter save = view->cursor;
	char *buf;

	if (!selecting())
		return NULL;

	*size = prepare_selection();
	buf = buffer_get_bytes(*size);
	view->cursor = save;
	return buf;
}

char *get_word_under_cursor(void)
{
	struct lineref lr;
	long i, ei, si = fetch_this_line(&view->cursor, &lr);

	while (si < lr.size) {
		i = si;
		if (u_is_word_char(u_get_char(lr.line, lr.size, &i)))
			break;
		si = i;
	}
	if (si == lr.size)
		return NULL;

	ei = si;
	while (si > 0) {
		i = si;
		if (!u_is_word_char(u_prev_char(lr.line, &i)))
			break;
		si = i;
	}
	while (ei < lr.size) {
		i = ei;
		if (!u_is_word_char(u_get_char(lr.line, lr.size, &i)))
			break;
		ei = i;
	}
	return xstrslice(lr.line, si, ei);
}

static struct buffer *buffer_new(const char *encoding)
{
	static int id;
	struct buffer *b;

	b = xnew0(struct buffer, 1);
	list_init(&b->blocks);
	b->cur_change = &b->change_head;
	b->saved_change = &b->change_head;
	b->id = ++id;
	b->newline = options.newline;
	if (encoding)
		b->encoding = xstrdup(encoding);

	memcpy(&b->options, &options, sizeof(struct common_options));
	b->options.brace_indent = 0;
	b->options.filetype = xstrdup("none");
	b->options.indent_regex = xstrdup("");

	ptr_array_add(&buffers, b);
	return b;
}

struct view *open_empty_buffer(void)
{
	struct buffer *b = buffer_new(charset);
	struct block *blk;
	struct view *v;

	// at least one block required
	blk = block_new(1);
	list_add_before(&blk->node, &b->blocks);

	v = window_add_buffer(b);
	v->cursor.head = &v->buffer->blocks;
	v->cursor.blk = BLOCK(v->buffer->blocks.next);

	set_display_filename(b, xstrdup("(No name)"));
	return v;
}

void free_buffer(struct buffer *b)
{
	struct list_head *item;

	ptr_array_remove(&buffers, b);

	if (b->locked)
		unlock_file(b->abs_filename);

	item = b->blocks.next;
	while (item != &b->blocks) {
		struct list_head *next = item->next;
		struct block *blk = BLOCK(item);

		free(blk->data);
		free(blk);
		item = next;
	}
	free_changes(&b->change_head);
	free(b->line_start_states.ptrs);
	free(b->views.ptrs);
	free(b->display_filename);
	free(b->abs_filename);
	free(b->encoding);
	free_local_options(&b->options);
	free(b);
}

struct view *buffer_get_view(struct buffer *b)
{
	struct view *v;
	int i;

	for (i = 0; i < b->views.count; i++) {
		v = b->views.ptrs[i];
		if (v->window == window) {
			// the file was already open in current window
			return v;
		}
	}
	// open the buffer in other window to current window
	v = window_add_buffer(b);
	v->cursor = ((struct view *)b->views.ptrs[0])->cursor;
	return v;
}

static int same_file(const struct stat *a, const struct stat *b)
{
	return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static struct buffer *find_buffer(const char *abs_filename)
{
	struct stat st;
	bool st_ok = stat(abs_filename, &st) == 0;
	int i;

	for (i = 0; i < buffers.count; i++) {
		struct buffer *b = buffers.ptrs[i];
		const char *f = b->abs_filename;

		if ((f != NULL && streq(f, abs_filename)) || (st_ok && same_file(&st, &b->st))) {
			return b;
		}
	}
	return NULL;
}

struct buffer *find_buffer_by_id(unsigned int id)
{
	int i;

	for (i = 0; i < buffers.count; i++) {
		struct buffer *b = buffers.ptrs[i];
		if (b->id == id) {
			return b;
		}
	}
	return NULL;
}

bool guess_filetype(void)
{
	char *interpreter = detect_interpreter(buffer);
	const char *ft = NULL;

	if (BLOCK(buffer->blocks.next)->size) {
		BLOCK_ITER(bi, &buffer->blocks);
		struct lineref lr;

		fill_line_ref(&bi, &lr);
		ft = find_ft(buffer->abs_filename, interpreter, lr.line, lr.size);
	} else if (buffer->abs_filename) {
		ft = find_ft(buffer->abs_filename, interpreter, NULL, 0);
	}
	free(interpreter);

	if (ft && !streq(ft, buffer->options.filetype)) {
		free(buffer->options.filetype);
		buffer->options.filetype = xstrdup(ft);
		return true;
	}
	return false;
}

void update_short_filename_cwd(struct buffer *b, const char *cwd)
{
	if (b->abs_filename) {
		if (cwd) {
			set_display_filename(b, short_filename_cwd(b->abs_filename, cwd));
		} else {
			// getcwd() failed
			set_display_filename(b, xstrdup(b->abs_filename));
		}
	}
}

void update_short_filename(struct buffer *b)
{
	set_display_filename(b, short_filename(b->abs_filename));
}

struct view *open_buffer(const char *filename, bool must_exist, const char *encoding)
{
	struct buffer *b;
	struct view *v;
	char *absolute;

	absolute = path_absolute(filename);
	if (!absolute) {
		error_msg("Failed to make absolute path: %s", strerror(errno));
		return NULL;
	}

	// already open?
	b = find_buffer(absolute);
	if (b) {
		if (!streq(absolute, b->abs_filename)) {
			char *s = short_filename(absolute);
			info_msg("%s and %s are the same file", s, b->display_filename);
			free(s);
		}
		free(absolute);
		return buffer_get_view(b);
	}

	b = buffer_new(encoding);
	b->abs_filename = absolute;
	update_short_filename(b);

	// /proc/$PID/fd/ contains symbolic links to files that have been opened
	// by process $PID. Some of the files may have been deleted but can still
	// be opened using the symbolic link but not by using the absolute path.
	if (load_buffer(b, must_exist, filename)) {
		free_buffer(b);
		return NULL;
	}

	v = window_add_buffer(b);
	v->cursor.head = &v->buffer->blocks;
	v->cursor.blk = BLOCK(v->buffer->blocks.next);
	return v;
}

void filetype_changed(void)
{
	set_file_options();
	syntax_changed();
}

void syntax_changed(void)
{
	struct syntax *syn = NULL;

	if (!buffer) {
		// syntax option was changed in config file
		return;
	}

	if (buffer->options.syntax) {
		/* even "none" can have syntax */
		syn = find_syntax(buffer->options.filetype);
		if (!syn)
			syn = load_syntax_by_filetype(buffer->options.filetype);
	}
	if (syn == buffer->syn)
		return;

	buffer->syn = syn;
	if (syn) {
		// start state of first line is constant
		struct ptr_array *s = &buffer->line_start_states;
		if (!s->alloc) {
			s->alloc = 64;
			s->ptrs = xnew(void *, s->alloc);
		}
		s->ptrs[0] = syn->states.ptrs[0];
		s->count = 1;
	}

	mark_all_lines_changed();
}

static void restore_cursor_from_history(void)
{
	int row, col;

	if (!find_file_in_history(buffer->abs_filename, &row, &col))
		return;

	move_to_line(row);
	move_to_column(col);
}

void setup_buffer(void)
{
	buffer->setup = true;
	guess_filetype();
	filetype_changed();
	if (buffer->options.detect_indent && buffer->abs_filename) {
		detect_indent(buffer);
	}
	if (buffer->options.file_history && buffer->abs_filename)
		restore_cursor_from_history();
}
