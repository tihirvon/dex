#include "window.h"
#include "util.h"
#include "term.h"

struct view *view;
struct buffer *buffer;
enum undo_merge undo_merge;

/* temporary buffer for searching etc. */
char *line_buffer;
size_t line_buffer_len;
static size_t line_buffer_alloc;

char *buffer_get_bytes(unsigned int *lenp)
{
	struct block *blk = view->cursor.blk;
	unsigned int offset = view->cursor.offset;
	unsigned int len = *lenp;
	unsigned int count = 0;
	char *buf = NULL;
	unsigned int alloc = 0;

	while (count < len) {
		unsigned int avail = blk->size - offset;
		if (avail > 0) {
			unsigned int c = len - count;

			if (c > avail)
				c = avail;
			alloc += c;
			xrenew(buf, alloc);
			memcpy(buf + count, blk->data + offset, c);
			count += c;
		}
		if (blk->node.next == &buffer->blocks)
			break;
		blk = BLOCK(blk->node.next);
		offset = 0;
	}
	*lenp = count;
	return buf;
}

static void line_buffer_add(size_t pos, const char *src, size_t count)
{
	size_t size = pos + count + 1;

	if (line_buffer_alloc < size) {
		line_buffer_alloc = ALLOC_ROUND(size);
		xrenew(line_buffer, line_buffer_alloc);
	}
	memcpy(line_buffer + pos, src, count);
}

void fetch_eol(const struct block_iter *bi)
{
	struct block *blk = bi->blk;
	unsigned int offset = bi->offset;
	size_t pos = 0;

	while (1) {
		unsigned int avail = blk->size - offset;
		char *src = blk->data + offset;
		char *ptr = memchr(src, '\n', avail);

		if (ptr) {
			line_buffer_add(pos, src, ptr - src);
			pos += ptr - src;
			break;
		}
		line_buffer_add(pos, src, avail);
		pos += avail;

		if (blk->node.next == bi->head)
			break;
		blk = BLOCK(blk->node.next);
		offset = 0;
	}
	line_buffer_add(pos, "", 1);
	line_buffer_len = pos;
}

unsigned int buffer_offset(void)
{
	return block_iter_get_offset(&view->cursor);
}

void move_offset(unsigned int offset)
{
	block_iter_goto_offset(&view->cursor, offset);
}

static struct buffer *buffer_new(void)
{
	struct buffer *b;

	b = xnew0(struct buffer, 1);
	list_init(&b->blocks);
	b->cur_change_head = &b->change_head;
	b->save_change_head = &b->change_head;
	b->utf8 = !!(term_flags & TERM_UTF8);

	b->options.auto_indent = options.auto_indent;
	b->options.expand_tab = options.expand_tab;
	b->options.indent_width = options.indent_width;
	b->options.tab_width = options.tab_width;
	b->options.trim_whitespace = options.trim_whitespace;

	b->newline = options.newline;
	return b;
}

static void buffer_set_callbacks(struct buffer *b)
{
	if (b->utf8) {
		b->next_char = block_iter_next_uchar;
		b->prev_char = block_iter_prev_uchar;
	} else {
		b->next_char = block_iter_next_byte;
		b->prev_char = block_iter_prev_byte;
	}
}

static struct view *empty_buffer(void)
{
	struct buffer *b = buffer_new();
	struct block *blk;

	// at least one block required
	blk = block_new(ALLOC_ROUND(1));
	list_add_before(&blk->node, &b->blocks);

	buffer_set_callbacks(b);
	return window_add_buffer(b);
}

static void read_crlf_blocks(struct buffer *b, const char *buf)
{
	size_t size = b->st.st_size;
	size_t pos = 0;

	while (pos < size) {
		size_t count = size - pos;
		struct block *blk;
		int s, d;

		if (count > BLOCK_MAX_SIZE)
			count = BLOCK_MAX_SIZE;

		blk = block_new(count);
		d = 0;
		for (s = 0; s < count; s++) {
			char ch = buf[pos + s];
			if (ch != '\r')
				blk->data[d++] = ch;
			if (ch == '\n')
				blk->nl++;
		}
		blk->size = d;
		b->nl += blk->nl;
		list_add_before(&blk->node, &b->blocks);
		pos += count;
	}
}

static void read_lf_blocks(struct buffer *b, const char *buf)
{
	size_t size = b->st.st_size;
	size_t pos = 0;

	while (pos < size) {
		size_t count = size - pos;
		struct block *blk;

		if (count > BLOCK_MAX_SIZE)
			count = BLOCK_MAX_SIZE;

		blk = block_new(count);
		blk->size = count;
		blk->nl = copy_count_nl(blk->data, buf + pos, blk->size);
		b->nl += blk->nl;
		list_add_before(&blk->node, &b->blocks);
		pos += count;
	}
}

static int read_blocks(struct buffer *b, int fd)
{
	char *nl, *buf = xmmap(fd, 0, b->st.st_size);

	if (!buf)
		return -1;

	nl = memchr(buf, '\n', b->st.st_size);
	if (nl > buf && nl[-1] == '\r') {
		b->newline = NEWLINE_DOS;
		read_crlf_blocks(b, buf);
	} else {
		read_lf_blocks(b, buf);
	}
	xmunmap(buf, b->st.st_size);
	return 0;
}

static void free_changes(struct buffer *b)
{
	struct change_head *ch = &b->change_head;

top:
	while (ch->nr_prev)
		ch = ch->prev[ch->nr_prev - 1];

	// ch is leaf now
	while (ch->next) {
		struct change_head *next = ch->next;

		free(((struct change *)ch)->buf);
		free(ch);

		ch = next;
		if (--ch->nr_prev)
			goto top;

		// we have become leaf
		free(ch->prev);
	}
}

void free_buffer(struct buffer *b)
{
	struct list_head *item;

	item = b->blocks.next;
	while (item != &b->blocks) {
		struct list_head *next = item->next;
		struct block *blk = BLOCK(item);

		free(blk->data);
		free(blk);
		item = next;
	}
	free_changes(b);

	free(b->filename);
	free(b->abs_filename);
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

struct view *open_buffer(const char *filename)
{
	struct buffer *b;
	struct view *v;
	char *absolute;
	int fd;

	if (!filename)
		return empty_buffer();

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
			return window_add_buffer(v->buffer);
		}
		// the file was already open in current window
		return v;
	}

	b = buffer_new();
	b->filename = xstrdup(filename);
	b->abs_filename = absolute;

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		if (errno == ENOENT)
			goto skip_read;
		fd = open(filename, O_RDONLY);
		if (fd < 0) {
			error_msg("Error opening %s: %s", filename, strerror(errno));
			free_buffer(b);
			return NULL;
		}
		b->ro = 1;
	}

	fstat(fd, &b->st);
	if (!S_ISREG(b->st.st_mode)) {
		error_msg("Can't open %s", get_file_type(b->st.st_mode));
		close(fd);
		free_buffer(b);
		return NULL;
	}

	if (read_blocks(b, fd)) {
		error_msg("Error reading %s: %s", filename, strerror(errno));
		close(fd);
		free_buffer(b);
		return NULL;
	}
	close(fd);
skip_read:
	if (list_empty(&b->blocks)) {
		struct block *blk = block_new(ALLOC_ROUND(1));
		list_add_before(&blk->node, &b->blocks);
	}
	buffer_set_callbacks(b);
	return window_add_buffer(b);
}

static int write_crlf(struct wbuf *wbuf, const char *buf, size_t size)
{
	while (size--) {
		char ch = *buf++;
		if (ch == '\n' && wbuf_write_ch(wbuf, '\r'))
			return -1;
		if (wbuf_write_ch(wbuf, ch))
			return -1;
	}
	return 0;
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
	struct block *blk;
	char tmp[PATH_MAX];
	WBUF(wbuf);
	int len, rc;

	len = strlen(filename);
	if (len + 8 > PATH_MAX) {
		errno = ENAMETOOLONG;
		error_msg("Error making temporary path name: %s", strerror(errno));
		return -1;
	}

	memcpy(tmp, filename, len);
	tmp[len] = '.';
	memset(tmp + len + 1, 'X', 6);
	tmp[len + 7] = 0;
	wbuf.fd = mkstemp(tmp);
	if (wbuf.fd < 0) {
		error_msg("Error creating temporary file: %s", strerror(errno));
		return -1;
	}
	fchmod(wbuf.fd, buffer->st.st_mode ? buffer->st.st_mode : 0666 & ~get_umask());

	rc = 0;
	list_for_each_entry(blk, &buffer->blocks, node) {
		if (blk->size) {
			if (newline == NEWLINE_DOS)
				rc = write_crlf(&wbuf, blk->data, blk->size);
			else
				rc = wbuf_write(&wbuf, blk->data, blk->size);
			if (rc)
				break;
		}
	}
	if (rc || wbuf_flush(&wbuf)) {
		error_msg("Write error: %s", strerror(errno));
		unlink(tmp);
		close(wbuf.fd);
		return -1;
	}
	if (rename(tmp, filename)) {
		error_msg("Rename failed: %s", strerror(errno));
		unlink(tmp);
		close(wbuf.fd);
		return -1;
	}
	close(wbuf.fd);

	buffer->save_change_head = buffer->cur_change_head;
	buffer->ro = 0;
	buffer->newline = newline;

	check_incomplete_last_line();
	return 0;
}
