#include "buffer.h"
#include "window.h"
#include "editor.h"
#include "change.h"
#include "block.h"
#include "move.h"
#include "util.h"
#include "wbuf.h"
#include "term.h"
#include "filetype.h"
#include "state.h"
#include "file-history.h"
#include "file-option.h"
#include "lock.h"
#include "config.h"
#include "regexp.h"

struct view *view;
struct buffer *buffer;
struct view *prev_view;

unsigned int update_flags;
int changed_line_min = INT_MAX;
int changed_line_max = -1;

void lines_changed(int min, int max)
{
	if (min > max) {
		int tmp = min;
		min = max;
		max = tmp;
	}

	if (min < changed_line_min)
		changed_line_min = min;
	if (max > changed_line_max)
		changed_line_max = max;
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

void update_preferred_x(void)
{
	update_cursor_x();
	view->preferred_x = view->cx_display;
}

void init_selection(struct selection_info *info)
{
	struct block_iter ei;
	uchar u;

	info->so = view->sel_so;
	info->eo = block_iter_get_offset(&view->cursor);
	info->si = view->cursor;
	block_iter_goto_offset(&info->si, info->so);
	info->swapped = 0;
	info->nr_lines = 1;
	info->nr_chars = 0;
	if (info->so > info->eo) {
		unsigned int o = info->so;
		info->so = info->eo;
		info->eo = o;
		info->si = view->cursor;
		info->swapped = 1;
	}

	ei = info->si;
	block_iter_skip_bytes(&ei, info->eo - info->so);
	if (block_iter_is_eof(&ei)) {
		if (info->so == info->eo)
			return;
		info->eo -= buffer_prev_char(&ei, &u);
	}
	if (view->selection == SELECT_LINES) {
		info->so -= block_iter_bol(&info->si);
		info->eo += block_iter_eat_line(&ei);
	} else {
		// character under cursor belongs to the selection
		info->eo += buffer_next_char(&ei, &u);
	}
}

void fill_selection_info(struct selection_info *info)
{
	struct block_iter bi = info->si;
	unsigned int nr_bytes = info->eo - info->so;
	uchar u, prev_char = 0;

	while (nr_bytes && buffer_next_char(&bi, &u)) {
		if (prev_char == '\n')
			info->nr_lines++;
		info->nr_chars++;
		prev_char = u;
		if (buffer->utf8)
			nr_bytes -= u_char_size(u);
		else
			nr_bytes--;
	}
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

static struct buffer *buffer_new(void)
{
	static int id;
	struct buffer *b;

	b = xnew0(struct buffer, 1);
	list_init(&b->blocks);
	b->cur_change_head = &b->change_head;
	b->save_change_head = &b->change_head;
	b->id = ++id;
	b->utf8 = !!(term_flags & TERM_UTF8);

	memcpy(&b->options, &options, sizeof(struct common_options));
	b->options.filetype = xstrdup("none");

	b->newline = options.newline;
	return b;
}

struct view *open_empty_buffer(void)
{
	struct buffer *b = buffer_new();
	struct block *blk;
	struct view *v;

	// at least one block required
	blk = block_new(1);
	list_add_before(&blk->node, &b->blocks);

	v = window_add_buffer(b);
	v->cursor.head = &v->buffer->blocks;
	v->cursor.blk = BLOCK(v->buffer->blocks.next);
	return v;
}

static size_t copy_strip_cr(char *dst, const char *src, size_t size)
{
	size_t si = 0;
	size_t di = 0;

	while (si < size) {
		char ch = src[si++];
		if (ch == '\r' && si < size && src[si] == '\n')
			ch = src[si++];
		dst[di++] = ch;
	}
	return di;
}

static size_t add_block(struct buffer *b, const char *buf, size_t size)
{
	const char *start = buf;
	const char *eof = buf + size;
	const char *end;
	struct block *blk;
	unsigned int lines = 0;

	do {
		const char *nl = memchr(start, '\n', eof - start);

		end = nl ? nl + 1 : eof;
		if (end - buf > 8192) {
			if (start == buf) {
				lines += !!nl;
				break;
			}
			end = start;
			break;
		}
		start = end;
		lines += !!nl;
	} while (end < eof);

	size = end - buf;
	blk = block_new(size);
	switch (b->newline) {
	case NEWLINE_UNIX:
		memcpy(blk->data, buf, size);
		blk->size = size;
		break;
	case NEWLINE_DOS:
		blk->size = copy_strip_cr(blk->data, buf, size);
		break;
	}
	blk->nl = lines;
	b->nl += lines;
	list_add_before(&blk->node, &b->blocks);
	return size;
}

static int read_blocks(struct buffer *b, int fd)
{
	size_t pos, size = b->st.st_size;
	char *nl, *buf = xmmap(fd, 0, size);

	if (!buf)
		return -1;

	nl = memchr(buf, '\n', size);
	if (nl > buf && nl[-1] == '\r')
		b->newline = NEWLINE_DOS;

	pos = 0;
	while (pos < size)
		pos += add_block(b, buf + pos, size - pos);

	for (pos = 0; pos < size; pos++) {
		if ((unsigned char)buf[pos] >= 0x80) {
			unsigned int idx = pos;
			uchar u = u_buf_get_char(buf, size, &idx);
			b->utf8 = !(u & U_INVALID_MASK);
			break;
		}
	}

	xmunmap(buf, b->st.st_size);
	return 0;
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

	free(b->filename);
	free(b->abs_filename);
	free_local_options(&b->options);
	free(b);
}

static struct view *find_view(const char *abs_filename)
{
	struct window *w;
	struct view *v;
	struct view *found = NULL;

	list_for_each_entry(w, &windows, node) {
		list_for_each_entry(v, &w->views, node) {
			const char *f = v->buffer->abs_filename;
			if (f && !strcmp(f, abs_filename)) {
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
	struct window *w;
	struct view *v;
	struct view *found = NULL;

	list_for_each_entry(w, &windows, node) {
		list_for_each_entry(v, &w->views, node) {
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

struct syntax *load_syntax_by_filename(const char *filename)
{
	struct syntax *syn = load_syntax_file(filename, 1);

	if (syn && editor_status != EDITOR_INITIALIZING)
		update_syntax_colors(syn);
	return syn;
}

struct syntax *load_syntax_by_filetype(const char *filetype)
{
	struct syntax *syn;
	char buf[1024];

	snprintf(buf, sizeof(buf), "%s/.editor/syntax/%s", home_dir, filetype);
	syn = load_syntax_file(buf, 0);
	if (!syn) {
		snprintf(buf, sizeof(buf), "%s/editor/syntax/%s", DATADIR, filetype);
		syn = load_syntax_file(buf, 0);
	}
	if (syn && editor_status != EDITOR_INITIALIZING)
		update_syntax_colors(syn);
	return syn;
}

static char *relative_filename(const char *f, const char *cwd)
{
	int i, tpos, tlen, dotdot, len, clen = 0;

	// length of common path
	while (cwd[clen] && cwd[clen] == f[clen])
		clen++;

	if (!cwd[clen] && f[clen] == '/') {
		// cwd    = /home/user
		// abs    = /home/user/project-a/file.c
		// common = /home/user
		return xstrdup(f + clen + 1);
	}

	// cwd    = /home/user/src/project
	// abs    = /home/user/save/parse.c
	// common = /home/user/s
	// find "save/parse.c"
	tpos = clen;
	while (tpos && f[tpos] != '/')
		tpos--;
	if (f[tpos] == '/')
		tpos++;

	// number of "../" needed
	dotdot = 1;
	for (i = clen + 1; cwd[i]; i++) {
		if (cwd[i] == '/')
			dotdot++;
	}

	tlen = strlen(f + tpos);
	len = dotdot * 3 + tlen;
	if (dotdot < 3 && len < strlen(f)) {
		char *filename = xnew(char, len + 1);
		for (i = 0; i < dotdot; i++)
			memcpy(filename + i * 3, "../", 3);
		memcpy(filename + dotdot * 3, f + tpos, tlen + 1);
		return filename;
	}
	return NULL;
}

void update_short_filename_cwd(struct buffer *b, const char *cwd)
{
	const char *absolute = b->abs_filename;
	int home_len;
	char *rel;

	if (!absolute)
		return;

	free(b->filename);
	rel = relative_filename(absolute, cwd);
	home_len = strlen(home_dir);
	if (!memcmp(absolute, home_dir, home_len) && absolute[home_len] == '/') {
		int abs_len = strlen(absolute);
		int len = abs_len - home_len + 1;
		if (!rel || len < strlen(rel)) {
			char *filename = xnew(char, len + 1);
			filename[0] = '~';
			memcpy(filename + 1, absolute + home_len, len);
			b->filename = filename;
			free(rel);
			return;
		}
	}

	if (rel)
		b->filename = rel;
	else
		b->filename = xstrdup(absolute);
}

void update_short_filename(struct buffer *b)
{
	char cwd[PATH_MAX];

	if (getcwd(cwd, sizeof(cwd))) {
		update_short_filename_cwd(b, cwd);
	} else {
		free(b->filename);
		b->filename = xstrdup(b->abs_filename);
	}
}

static int load_buffer(struct buffer *b, int must_exist);

struct view *open_buffer(const char *filename, int must_exist)
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

	b = buffer_new();
	b->abs_filename = absolute;
	update_short_filename(b);

	if (load_buffer(b, must_exist)) {
		free_buffer(b);
		return NULL;
	}

	v = window_add_buffer(b);
	v->cursor.head = &v->buffer->blocks;
	v->cursor.blk = BLOCK(v->buffer->blocks.next);
	return v;
}

static int load_buffer(struct buffer *b, int must_exist)
{
	const char *filename = b->abs_filename;
	int fd;

	if (options.lock_files) {
		if (lock_file(filename)) {
			b->ro = 1;
		} else {
			b->locked = 1;
		}
	}

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		if (errno != ENOENT) {
			error_msg("Error opening %s: %s", filename, strerror(errno));
			return -1;
		}
		if (must_exist) {
			error_msg("File %s does not exist.", filename);
			return -1;
		}
	} else {
		fstat(fd, &b->st);
		if (!S_ISREG(b->st.st_mode)) {
			error_msg("Can't open %s %s", get_file_type(b->st.st_mode), filename);
			close(fd);
			return -1;
		}

		if (read_blocks(b, fd)) {
			error_msg("Error reading %s: %s", filename, strerror(errno));
			close(fd);
			return -1;
		}
		close(fd);

		if (!b->ro && access(filename, W_OK)) {
			error_msg("No write permission to %s, marking read-only.", filename);
			b->ro = 1;
		}
	}
	if (list_empty(&b->blocks)) {
		struct block *blk = block_new(1);
		list_add_before(&blk->node, &b->blocks);
	}
	return 0;
}

void filetype_changed(void)
{
	set_file_options();
	syntax_changed();
}

void syntax_changed(void)
{
	const struct syntax *syn = NULL;

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
		buffer->first_hole = 1;
	}
}

static void restore_cursor_from_history(void)
{
	int x, y;

	if (!find_file_in_history(buffer->abs_filename, &x, &y))
		return;

	move_to_line(y + 1);
	move_to_column(x + 1);
}

void setup_buffer(void)
{
	buffer->setup = 1;
	guess_filetype();
	filetype_changed();
	if (buffer->options.file_history && buffer->abs_filename)
		restore_cursor_from_history();
}

static int write_crlf(struct wbuf *wbuf, const char *buf, int size)
{
	int written = 0;

	while (size--) {
		char ch = *buf++;
		if (ch == '\n') {
			if (wbuf_write_ch(wbuf, '\r'))
				return -1;
			written++;
		}
		if (wbuf_write_ch(wbuf, ch))
			return -1;
		written++;
	}
	return written;
}

static mode_t get_umask(void)
{
	// Wonderful get-and-set API
	mode_t old = umask(0);
	umask(old);
	return old;
}

static void check_incomplete_last_line(void)
{
	struct block *blk = BLOCK(buffer->blocks.prev);
	if (blk->size && blk->data[blk->size - 1] != '\n')
		info_msg("Incomplete last line");
}

int save_buffer(const char *filename, enum newline_sequence newline)
{
	/* try to use temporary file first, safer */
	int ren = 1;
	struct block *blk;
	char tmp[PATH_MAX];
	WBUF(wbuf);
	int rc, len;
	unsigned int size;
	mode_t mode = buffer->st.st_mode ? buffer->st.st_mode : 0666 & ~get_umask();

	if (!strncmp(filename, "/tmp/", 5))
		ren = 0;

	len = strlen(filename);
	if (len + 8 > PATH_MAX)
		ren = 0;
	if (ren) {
		memcpy(tmp, filename, len);
		tmp[len] = '.';
		memset(tmp + len + 1, 'X', 6);
		tmp[len + 7] = 0;
		wbuf.fd = mkstemp(tmp);
		if (wbuf.fd < 0) {
			ren = 0;
		} else {
			fchmod(wbuf.fd, mode);
		}
	}
	if (!ren) {
		wbuf.fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY, mode);
		if (wbuf.fd < 0) {
			error_msg("Error opening file: %s", strerror(errno));
			return -1;
		}
	}

	rc = 0;
	size = 0;
	if (newline == NEWLINE_DOS) {
		list_for_each_entry(blk, &buffer->blocks, node) {
			rc = write_crlf(&wbuf, blk->data, blk->size);
			if (rc < 0)
				break;
			size += rc;
		}
	} else {
		list_for_each_entry(blk, &buffer->blocks, node) {
			rc = wbuf_write(&wbuf, blk->data, blk->size);
			if (rc)
				break;
			size += blk->size;
		}
	}
	if (rc < 0 || wbuf_flush(&wbuf)) {
		error_msg("Write error: %s", strerror(errno));
		if (ren)
			unlink(tmp);
		close(wbuf.fd);
		return -1;
	}
	if (!ren && ftruncate(wbuf.fd, size)) {
		error_msg("Truncate failed: %s", strerror(errno));
		close(wbuf.fd);
		return -1;
	}
	if (ren && rename(tmp, filename)) {
		error_msg("Rename failed: %s", strerror(errno));
		unlink(tmp);
		close(wbuf.fd);
		return -1;
	}
	fstat(wbuf.fd, &buffer->st);
	close(wbuf.fd);

	buffer->save_change_head = buffer->cur_change_head;
	buffer->ro = 0;
	buffer->newline = newline;

	check_incomplete_last_line();
	return 0;
}
