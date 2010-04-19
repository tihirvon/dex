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
#include "buffer-highlight.h"
#include "highlight.h"
#include "file-history.h"
#include "file-option.h"
#include "lock.h"
#include "config.h"

struct view *view;
struct buffer *buffer;
struct view *prev_view;
enum undo_merge undo_merge;

void update_preferred_x(void)
{
	update_cursor_x();
	view->preferred_x = view->cx_display;
}

void init_selection(struct selection_info *info)
{
	uchar u;

	info->si = view->cursor;
	info->ei = view->sel;
	info->so = block_iter_get_offset(&info->si);
	info->eo = block_iter_get_offset(&info->ei);
	info->swapped = 0;
	info->nr_lines = 1;
	info->nr_chars = 0;
	if (info->so > info->eo) {
		struct block_iter bi = info->si;
		unsigned int o = info->so;
		info->si = info->ei;
		info->ei = bi;
		info->so = info->eo;
		info->eo = o;
		info->swapped = 1;
	}
	if (block_iter_eof(&info->ei)) {
		if (info->so == info->eo)
			return;
		info->eo -= buffer_prev_char(&info->ei, &u);
	}
	if (view->selection == SELECT_LINES) {
		info->so -= block_iter_bol(&info->si);
		info->eo += count_bytes_eol(&info->ei);
	} else {
		// character under cursor belongs to the selection
		info->eo += buffer_next_char(&info->ei, &u);
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
	struct buffer *b;

	b = xnew0(struct buffer, 1);
	list_init(&b->blocks);
	b->cur_change_head = &b->change_head;
	b->save_change_head = &b->change_head;
	b->utf8 = !!(term_flags & TERM_UTF8);

	memcpy(&b->options, &options, sizeof(struct common_options));
	b->options.filetype = xstrdup("none");

	b->newline = options.newline;
	list_init(&b->hl_head);
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
	free_hl_list(&b->hl_head);

	free(b->filename);
	free(b->abs_filename);
	free_local_options(&b->options);
	free(b);
}

static struct view *find_view(const char *abs_filename)
{
	struct window *w;
	struct view *v;

	// open in current window?
	list_for_each_entry(v, &window->views, node) {
		const char *f = v->buffer->abs_filename;
		if (f && !strcmp(f, abs_filename))
			return v;
	}

	// open in any other window?
	list_for_each_entry(w, &windows, node) {
		if (w == window)
			continue;
		list_for_each_entry(v, &w->views, node) {
			const char *f = v->buffer->abs_filename;
			if (f && !strcmp(f, abs_filename))
				return v;
		}
	}
	return NULL;
}

int guess_filetype(void)
{
	const char *ft = NULL;

	if (BLOCK(buffer->blocks.next)->size) {
		struct lineref lr;
		struct block_iter bi;

		bi.blk = BLOCK(buffer->blocks.next);
		bi.head = &buffer->blocks;
		bi.offset = 0;
		fill_line_ref(&bi, &lr);
		ft = find_ft(buffer->abs_filename, lr.line, lr.size);
	} else if (buffer->abs_filename) {
		ft = find_ft(buffer->abs_filename, NULL, 0);
	}
	if (ft && strcmp(ft, buffer->options.filetype)) {
		free(buffer->options.filetype);
		buffer->options.filetype = xstrdup(ft);
		return 1;
	}
	return 0;
}

struct syntax *load_syntax(const char *filetype)
{
	char filename[1024];
	struct syntax *syn;

	snprintf(filename, sizeof(filename), "%s/.editor/syntax/%s", home_dir, filetype);
	if (read_config(filename, 0)) {
		snprintf(filename, sizeof(filename), "%s/editor/syntax/%s", DATADIR, filetype);
		if (read_config(filename, 0))
			return NULL;
	}
	syn = find_syntax(filetype);
	if (syn)
		update_syntax_colors();
	return syn;
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
	b->filename = xstrdup(filename);
	b->abs_filename = absolute;
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
	int fd;

	if (options.lock_files) {
		if (lock_file(b->abs_filename)) {
			b->ro = 1;
		} else {
			b->locked = 1;
		}
	}

	fd = open(b->filename, O_RDONLY);
	if (fd < 0) {
		if (errno != ENOENT) {
			error_msg("Error opening %s: %s", b->filename, strerror(errno));
			return -1;
		}
		if (must_exist) {
			error_msg("File %s does not exist.", b->filename);
			return -1;
		}
	} else {
		fstat(fd, &b->st);
		if (!S_ISREG(b->st.st_mode)) {
			error_msg("Can't open %s %s", get_file_type(b->st.st_mode), b->filename);
			close(fd);
			return -1;
		}

		if (read_blocks(b, fd)) {
			error_msg("Error reading %s: %s", b->filename, strerror(errno));
			close(fd);
			return -1;
		}
		close(fd);

		if (!b->ro && access(b->abs_filename, W_OK)) {
			error_msg("No write permission to %s, marking read-only.", b->filename);
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

	if (buffer->options.syntax) {
		/* even "none" can have syntax */
		syn = find_syntax(buffer->options.filetype);
		if (!syn)
			syn = load_syntax(buffer->options.filetype);
	}
	if (syn == buffer->syn)
		return;

	buffer->syn = syn;
	free_hl_list(&buffer->hl_head);
	highlight_buffer(buffer);

	update_flags |= UPDATE_FULL;
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
