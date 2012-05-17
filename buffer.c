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
#include "regexp.h"
#include "selection.h"
#include "path.h"

struct buffer *buffer;
struct view *prev_view;

int everything_changed;

static void set_display_filename(struct buffer *b, char *name)
{
	free(b->display_filename);
	if (term_utf8) {
		b->display_filename = name;
	} else {
		b->display_filename = xstrdup(filename_to_utf8(name));
		free(name);
	}
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

unsigned int count_nl(const char *buf, unsigned int size)
{
	const char *end = buf + size;
	unsigned int nl = 0;

	while (buf < end) {
		buf = memchr(buf, '\n', end - buf);
		if (!buf)
			break;
		buf++;
		nl++;
	}
	return nl;
}

char *buffer_get_bytes(unsigned int len)
{
	struct block *blk = view->cursor.blk;
	unsigned int offset = view->cursor.offset;
	unsigned int pos = 0;
	char *buf;

	if (!len)
		return NULL;

	buf = xnew(char, len);
	while (pos < len) {
		unsigned int avail = blk->size - offset;
		unsigned int count = len - pos;

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

char *get_selection(unsigned int *size)
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
	unsigned int ei, si = fetch_this_line(&view->cursor, &lr);

	while (si < lr.size && !is_word_byte(lr.line[si]))
		si++;

	if (si == lr.size)
		return NULL;

	ei = si;
	while (si > 0 && is_word_byte(lr.line[si - 1]))
		si--;
	while (ei + 1 < lr.size && is_word_byte(lr.line[ei + 1]))
		ei++;
	return xstrslice(lr.line, si, ei + 1);
}

static struct buffer *buffer_new(const char *encoding)
{
	static int id;
	struct buffer *b;

	b = xnew0(struct buffer, 1);
	list_init(&b->blocks);
	b->cur_change_head = &b->change_head;
	b->save_change_head = &b->change_head;
	b->id = ++id;
	if (encoding)
		b->encoding = xstrdup(encoding);

	memcpy(&b->options, &options, sizeof(struct common_options));
	b->options.brace_indent = 0;
	b->options.filetype = xstrdup("none");
	b->options.indent_regex = xstrdup("");

	b->newline = options.newline;
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

static int same_file(struct stat *st, struct buffer *b)
{
	return st->st_dev == b->st_dev && st->st_ino == b->st_ino;
}

static struct view *find_view(const char *abs_filename)
{
	struct view *found = NULL;
	struct stat st;
	int i, j, st_ok;

	st_ok = stat(abs_filename, &st) == 0;
	for (i = 0; i < windows.count; i++) {
		for (j = 0; j < WINDOW(i)->views.count; j++) {
			struct view *v = VIEW(i, j);
			const char *f = v->buffer->abs_filename;
			if (f == NULL)
				continue;
			if (!strcmp(f, abs_filename) || (st_ok && same_file(&st, v->buffer))) {
				// found in current window?
				if (v->window == window)
					return v;
				found = v;
			}
		}
	}
	return found;
}

struct view *find_view_by_buffer_id(unsigned int buffer_id)
{
	struct view *v, *found = NULL;
	int i, j;

	for (i = 0; i < windows.count; i++) {
		for (j = 0; j < WINDOW(i)->views.count; j++) {
			v = VIEW(i, j);
			if (buffer_id == v->buffer->id) {
				// found in current window?
				if (v->window == window)
					return v;

				found = v;
			}
		}
	}
	if (!found)
		return NULL;

	// open the buffer in other window to current window
	v = window_add_buffer(found->buffer);
	v->cursor = found->cursor;
	return v;
}

static int next_line(struct block_iter *bi, struct lineref *lr)
{
	if (!block_iter_eat_line(bi))
		return 0;
	fill_line_ref(bi, lr);
	return 1;
}

/*
 * Parse #! line and return interpreter name without vesion number.
 * For example if file's first line is "#!/usr/bin/env python2" then
 * "python" is returned.
 */
static char *get_interpreter(void)
{
	struct block_iter bi;
	struct lineref lr;
	char *ret;
	int n;

	buffer_bof(&bi);
	fill_line_ref(&bi, &lr);
	n = regexp_match("^#!\\s*/.*(/env\\s+|/)([a-zA-Z_-]+)[0-9.]*(\\s|$)",
		lr.line, lr.size);
	if (!n)
		return NULL;

	ret = xstrdup(regexp_matches[2]);
	free_regexp_matches();

	if (strcmp(ret, "sh"))
		return ret;

	/*
	 * #!/bin/sh
	 * # the next line restarts using wish \
	 * exec wish "$0" "$@"
	 */
	if (!next_line(&bi, &lr) || !regexp_match_nosub("^#.*\\\\$", lr.line, lr.size))
		return ret;

	if (!next_line(&bi, &lr) || !regexp_match_nosub("^exec\\s+wish\\s+", lr.line, lr.size))
		return ret;

	free(ret);
	return xstrdup("wish");
}

int guess_filetype(void)
{
	char *interpreter = get_interpreter();
	const char *ft = NULL;

	if (BLOCK(buffer->blocks.next)->size) {
		struct lineref lr;
		struct block_iter bi;

		buffer_bof(&bi);
		fill_line_ref(&bi, &lr);
		ft = find_ft(buffer->abs_filename, interpreter, lr.line, lr.size);
	} else if (buffer->abs_filename) {
		ft = find_ft(buffer->abs_filename, interpreter, NULL, 0);
	}
	free(interpreter);

	if (ft && strcmp(ft, buffer->options.filetype)) {
		free(buffer->options.filetype);
		buffer->options.filetype = xstrdup(ft);
		return 1;
	}
	return 0;
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

struct view *open_buffer(const char *filename, int must_exist, const char *encoding)
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
	v = find_view(absolute);
	if (v) {
		if (strcmp(absolute, v->buffer->abs_filename)) {
			char *s = short_filename(absolute);
			info_msg("%s and %s are the same file", s, v->buffer->display_filename);
			free(s);
		}
		free(absolute);
		if (v->window != window) {
			// open the buffer in other window to current window
			struct view *new = window_add_buffer(v->buffer);
			new->cursor = v->cursor;
			return new;
		}
		// the file was already open in current window
		return v;
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
	buffer->setup = 1;
	guess_filetype();
	filetype_changed();
	if (buffer->options.file_history && buffer->abs_filename)
		restore_cursor_from_history();
}
