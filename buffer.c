#include "window.h"
#include "term.h"

struct view *view;
struct buffer *buffer;
enum undo_merge undo_merge;

/* temporary buffer for searching etc. */
char *line_buffer;
size_t line_buffer_len;
static size_t line_buffer_alloc;

struct block *block_new(int alloc)
{
	struct block *blk = xnew0(struct block, 1);

	if (alloc)
		blk->data = xnew(char, alloc);
	blk->alloc = alloc;
	return blk;
}

static void free_block(struct block *blk)
{
	free(blk->data);
	free(blk);
}

void delete_block(struct block *blk)
{
	list_del(&blk->node);
	free_block(blk);
}

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

		if (blk->node.next == &buffer->blocks)
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

static struct change *new_change(void)
{
	struct change_head *head = buffer->cur_change_head;
	struct change *change = xcalloc(sizeof(struct change));

	change->head.next = head;
	xrenew(head->prev, head->nr_prev + 1);
	head->prev[head->nr_prev++] = &change->head;

	buffer->cur_change_head = &change->head;
	return change;
}

void record_insert(unsigned int len)
{
	struct change *change = (struct change *)buffer->cur_change_head;

	if (undo_merge == UNDO_MERGE_INSERT && change && !change->del_count) {
		change->ins_count += len;
		return;
	}

	change = new_change();
	change->offset = buffer_offset();
	change->ins_count = len;
}

void record_delete(char *buf, unsigned int len, int move_after)
{
	struct change *change = (struct change *)buffer->cur_change_head;

	if (change && !change->ins_count) {
		if (undo_merge == UNDO_MERGE_DELETE) {
			xrenew(change->buf, change->del_count + len);
			memcpy(change->buf + change->del_count, buf, len);
			change->del_count += len;
			free(buf);
			return;
		}
		if (undo_merge == UNDO_MERGE_BACKSPACE) {
			xrenew(buf, len + change->del_count);
			memcpy(buf + len, change->buf, change->del_count);
			change->del_count += len;
			free(change->buf);
			change->buf = buf;
			change->offset -= len;
			return;
		}
	}

	change = new_change();
	change->offset = buffer_offset();
	change->del_count = len;
	change->move_after = move_after;
	change->buf = buf;
}

void record_replace(char *deleted, unsigned int del_count, unsigned int ins_count)
{
	struct change *change = new_change();
	change->offset = buffer_offset();
	change->ins_count = ins_count;
	change->del_count = del_count;
	change->buf = deleted;
}

static void move_offset(unsigned int offset)
{
	struct block *blk;

	list_for_each_entry(blk, &buffer->blocks, node) {
		if (offset <= blk->size) {
			view->cursor.blk = blk;
			view->cursor.offset = offset;
			return;
		}
		offset -= blk->size;
	}
}

static void reverse_change(struct change *change)
{
	move_offset(change->offset);
	if (!change->ins_count) {
		// convert delete to insert
		do_insert(change->buf, change->del_count);
		if (change->move_after)
			move_offset(change->offset + change->del_count);
		change->ins_count = change->del_count;
		change->del_count = 0;
		update_preferred_x();
		free(change->buf);
		change->buf = NULL;
	} else if (change->del_count) {
		// reverse replace
		unsigned int count = change->ins_count;
		char *buf = buffer_get_bytes(&count);

		do_delete(change->ins_count);
		do_insert(change->buf, change->del_count);
		free(change->buf);
		change->buf = buf;
		change->ins_count = change->del_count;
		change->del_count = count;
	} else {
		// convert insert to delete
		unsigned int count = change->ins_count;
		change->buf = buffer_get_bytes(&count);
		do_delete(change->ins_count);
		change->del_count = change->ins_count;
		change->ins_count = 0;
		update_preferred_x();
	}
}

void undo(void)
{
	struct change *change;

	undo_merge = UNDO_MERGE_NONE;
	if (!buffer->cur_change_head->next)
		return;

	change = (struct change *)buffer->cur_change_head;
	reverse_change(change);
	buffer->cur_change_head = buffer->cur_change_head->next;
}

void redo(void)
{
	struct change_head *head;
	struct change *change;

	undo_merge = UNDO_MERGE_NONE;
	if (!buffer->cur_change_head->prev)
		return;

	head = buffer->cur_change_head->prev[buffer->cur_change_head->nr_prev - 1];
	change = (struct change *)head;
	reverse_change(change);
	buffer->cur_change_head = head;
}

static struct buffer *buffer_new(const char *filename)
{
	struct buffer *b;
	char *absolute = NULL;

	if (filename) {
		absolute = path_absolute(filename);
		if (!absolute) {
			error_msg("Failed to make absolute path.");
			return NULL;
		}
	}
	b = xnew0(struct buffer, 1);
	if (filename) {
		b->filename = xstrdup(filename);
		b->abs_filename = absolute;
	}
	list_init(&b->blocks);
	b->cur_change_head = &b->change_head;
	b->save_change_head = &b->change_head;
	b->tab_width = 8;
	b->utf8 = !!(term_flags & TERM_UTF8);
	return b;
}

static int read_blocks(struct buffer *b, int fd)
{
	int r = 0;

	while (1) {
		struct block *blk;
		ssize_t rc;
		int size = BLOCK_SIZE;

		if (size > b->st.st_size - r)
			size = b->st.st_size - r;

		blk = block_new(size);
		rc = xread(fd,  blk->data, size);

		if (rc <= 0) {
			free(blk->data);
			free(blk);
			if (rc < 0)
				return rc;
			break;
		}
		blk->size = rc;
		r += rc;

		blk->nl = count_nl(blk->data, blk->size);
		b->nl += blk->nl;
		list_add_before(&blk->node, &b->blocks);
	}
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

static void free_buffer(struct buffer *b)
{
	struct list_head *item;

	item = b->blocks.next;
	while (item != &b->blocks) {
		struct list_head *next = item->next;
		free_block(container_of(item, struct block, node));
		item = next;
	}
	free_changes(b);

	free(b->filename);
	free(b->abs_filename);
	free(b);
}

static int same_buffer(struct buffer *a, struct buffer *b)
{
	if (a->st.st_mode && b->st.st_mode &&
	    a->st.st_dev == b->st.st_dev &&
	    a->st.st_ino == b->st.st_ino)
		return 1;
	if (a->abs_filename && b->abs_filename)
		return !strcmp(a->abs_filename, b->abs_filename);
	return 0;
}

static struct view *find_view(struct buffer *b)
{
	struct window *w;
	struct view *v;

	// open in current window?
	list_for_each_entry(v, &window->views, node) {
		if (same_buffer(v->buffer, b))
			return v;
	}

	// open in any other window?
	list_for_each_entry(w, &windows, node) {
		if (w == window)
			continue;
		list_for_each_entry(v, &w->views, node) {
			if (same_buffer(v->buffer, b))
				return v;
		}
	}
	return NULL;
}

struct view *open_buffer(const char *filename)
{
	struct buffer *b;

	b = buffer_new(filename);
	if (!b)
		return NULL;
	if (filename) {
		struct view *v;
		int fd, ro = 0;

		fd = open(filename, O_RDWR);
		if (fd < 0) {
			ro = 1;
			fd = open(filename, O_RDONLY);
		}
		if (fd < 0) {
			if (errno != ENOENT) {
				free_buffer(b);
				return NULL;
			}

			v = find_view(b);
		} else {
			b->ro = ro;
			fstat(fd, &b->st);
			if (!S_ISREG(b->st.st_mode)) {
				int err = S_ISDIR(b->st.st_mode) ? EISDIR : EINVAL;
				close(fd);
				free_buffer(b);
				errno = err;
				return NULL;
			}
			v = find_view(b);
			if (!v && read_blocks(b, fd)) {
				close(fd);
				free_buffer(b);
				return NULL;
			}
			close(fd);
		}

		if (v) {
			free_buffer(b);
			if (v->window != window) {
				// open the buffer in other window to current window
				return window_add_buffer(v->buffer);
			}
			// the file was already open in current window
			return v;
		}
	}

	if (list_empty(&b->blocks)) {
		struct block *blk = block_new(ALLOC_ROUND(1));
		list_add_before(&blk->node, &b->blocks);
	}

	if (b->utf8) {
		b->next_char = block_iter_next_uchar;
		b->prev_char = block_iter_prev_uchar;
		b->get_char = block_iter_get_uchar;
	} else {
		b->next_char = block_iter_next_byte;
		b->prev_char = block_iter_prev_byte;
		b->get_char = block_iter_get_byte;
	}

	return window_add_buffer(b);
}

void save_buffer(void)
{
	struct block *blk;
	char *filename;
	int len, fd;

	if (!buffer->filename) {
		return;
	}
	if (buffer->ro) {
		return;
	}

	len = strlen(buffer->filename);
	filename = xnew(char, len + 8);
	memcpy(filename, buffer->filename, len);
	filename[len] = '.';
	memset(filename + len + 1, 'X', 6);
	filename[len + 7] = 0;
	fd = mkstemp(filename);
	if (fd < 0) {
		free(filename);
		return;
	}
	list_for_each_entry(blk, &buffer->blocks, node) {
		if (blk->size)
			xwrite(fd, blk->data, blk->size);
	}
	close(fd);
	if (rename(filename, buffer->filename)) {
		unlink(filename);
		free(filename);
		return;
	}

	free(filename);
	buffer->save_change_head = buffer->cur_change_head;
}
