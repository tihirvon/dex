#include "buffer.h"
#include "xmalloc.h"
#include "term.h"

LIST_HEAD(windows);
struct window *window;
struct view *view;
struct buffer *buffer;
enum undo_merge undo_merge;

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
	struct block *blk = view->cblk;
	unsigned int offset = view->coffset;
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

unsigned int buffer_get_offset(struct block *stop_blk, unsigned int stop_offset)
{
	struct block *blk;
	unsigned int offset = 0;

	list_for_each_entry(blk, &buffer->blocks, node) {
		if (blk == stop_blk)
			break;
		offset += blk->size;
	}
	return offset + stop_offset;
}

unsigned int buffer_offset(void)
{
	return buffer_get_offset(view->cblk, view->coffset);
}

static void add_change(struct change *change, struct change_head *head)
{
	change->head.next = head;
	xrenew(head->prev, head->nr_prev + 1);
	head->prev[head->nr_prev++] = &change->head;
}

void record_change(unsigned int offset, char *buf, unsigned int len)
{
	struct change *change;

	if (undo_merge && buffer->cur_change_head->next) {
		change = (struct change *)buffer->cur_change_head;
		if (undo_merge == UNDO_MERGE_INSERT && !buf && !change->buf) {
			change->count += len;
			return;
		}
		if (buf && change->buf) {
			if (undo_merge == UNDO_MERGE_DELETE) {
				xrenew(change->buf, change->count + len);
				memcpy(change->buf + change->count, buf, len);
				change->count += len;
				free(buf);
				return;
			}
			if (undo_merge == UNDO_MERGE_BACKSPACE) {
				xrenew(buf, len + change->count);
				memcpy(buf + len, change->buf, change->count);
				change->count += len;
				free(change->buf);
				change->buf = buf;
				change->offset -= len;
				return;
			}
		}
	}

	change = xmalloc(sizeof(struct change));
	change->offset = offset;
	change->count = len;
	change->buf = buf;
	change->head.prev = NULL;
	change->head.nr_prev = 0;

	add_change(change, buffer->cur_change_head);
	buffer->cur_change_head = &change->head;
}

static void move_offset(unsigned int offset)
{
	struct block *blk;

	list_for_each_entry(blk, &buffer->blocks, node) {
		if (offset <= blk->size) {
			view->cblk = blk;
			view->coffset = offset;
			return;
		}
		offset -= blk->size;
	}
}

static void reverse_change(struct change *change)
{
	move_offset(change->offset);
	if (change->buf) {
		do_insert(change->buf, change->count);
		update_preferred_x();
		free(change->buf);
		change->buf = NULL;
	} else {
		unsigned int count = change->count;
		change->buf = buffer_get_bytes(&count);
		do_delete(change->count);
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
	struct buffer *b = xnew0(struct buffer, 1);
	if (filename)
		b->filename = xstrdup(filename);
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
		int size = BLOCK_SIZE;

		if (size > b->size - r)
			size = b->size - r;

		blk = block_new(size);
		blk->size = xread(fd,  blk->data, size);

		if (blk->size <= 0) {
			free(blk->data);
			free(blk);
			if (blk->size == -1)
				return -1;
			break;
		}
		r += blk->size;

		blk->nl = count_nl(blk->data, blk->size);
		b->nl += blk->nl;
		list_add_before(&blk->node, &b->blocks);
	}
	b->size = r;
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

	item = &b->blocks;
	while (item != &b->blocks) {
		struct list_head *next = item->next;
		free_block(container_of(item, struct block, node));
		item = next;
	}
	free_changes(b);

	free(b->filename);
	free(b);
}

struct buffer *open_buffer(const char *filename)
{
	struct buffer *b;

	b = buffer_new(filename);
	if (filename) {
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
		} else {
			struct stat st;

			fstat(fd, &st);
			b->size = st.st_size;
			b->ro = ro;
			if (read_blocks(b, fd)) {
				free_buffer(b);
				return NULL;
			}
			close(fd);
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

	window_add_buffer(b);
	return b;
}

void save_buffer(void)
{
	struct block *blk;
	char *filename;
	int len, fd;

	len = strlen(buffer->filename);
	filename = xnew(char, len + 8);
	memcpy(filename, buffer->filename, len);
	filename[len] = '.';
	memset(filename + len + 1, 'X', 6);
	filename[len + 7] = 0;
	fd = mkstemp(filename);
	if (fd < 0) {
		return;
	}
	list_for_each_entry(blk, &buffer->blocks, node) {
		if (blk->size)
			xwrite(fd, blk->data, blk->size);
	}
	close(fd);
	if (rename(filename, buffer->filename)) {
	}

	buffer->save_change_head = buffer->cur_change_head;
}
