#include "block.h"
#include "buffer.h"
#include "buffer-highlight.h"

static inline size_t ALLOC_ROUND(size_t size)
{
	return ROUND_UP(size, 64);
}

struct block *block_new(unsigned int alloc)
{
	struct block *blk = xnew0(struct block, 1);

	alloc = ALLOC_ROUND(alloc);
	blk->data = xnew(char, alloc);
	blk->alloc = alloc;
	return blk;
}

static void delete_block(struct block *blk)
{
	list_del(&blk->node);
	free(blk->data);
	free(blk);
}

static unsigned int copy_count_nl(char *dst, const char *src, unsigned int len)
{
	unsigned int i, nl = 0;
	for (i = 0; i < len; i++) {
		dst[i] = src[i];
		if (src[i] == '\n')
			nl++;
	}
	return nl;
}

static unsigned int insert_to_current(const char *buf, unsigned int len)
{
	struct block *blk = view->cursor.blk;
	unsigned int offset = view->cursor.offset;
	unsigned int size = blk->size + len;
	unsigned int nl;

	if (size > blk->alloc) {
		blk->alloc = ALLOC_ROUND(size);
		xrenew(blk->data, blk->alloc);
	}
	memmove(blk->data + offset + len, blk->data + offset, blk->size - offset);
	nl = copy_count_nl(blk->data + offset, buf, len);
	blk->nl += nl;
	blk->size = size;
	return nl;
}

static unsigned int insert_to_next(const char *buf, unsigned int len)
{
	BUG_ON(view->cursor.offset != view->cursor.blk->size);
	BUG_ON(view->cursor.blk->node.next == &buffer->blocks);
	view->cursor.blk = BLOCK(view->cursor.blk->node.next);
	view->cursor.offset = 0;
	return insert_to_current(buf, len);
}

static unsigned int append_to_current(const char *buf, unsigned int len)
{
	struct block *blk = view->cursor.blk;
	unsigned int size = blk->size + len;
	unsigned int nl;

	if (size > blk->alloc) {
		blk->alloc = ALLOC_ROUND(size);
		xrenew(blk->data, blk->alloc);
	}
	nl = copy_count_nl(blk->data + blk->size, buf, len);
	blk->nl += nl;
	blk->size = size;
	return nl;
}

static unsigned int add_new_block(const char *buf, unsigned int len)
{
	struct block *blk = block_new(len);

	blk->nl = copy_count_nl(blk->data, buf, len);
	blk->size = len;
	list_add_after(&blk->node, &view->cursor.blk->node);
	return blk->nl;
}

static unsigned int insert_bytes(const char *buf, unsigned int len)
{
	struct block *blk = view->cursor.blk;
	struct block *next = NULL;
	unsigned int offset = view->cursor.offset;

	if (offset < blk->size)
		return insert_to_current(buf, len);

	if (!blk->size || blk->data[blk->size - 1] != '\n') {
		// must append to this block
		return append_to_current(buf, len);
	}

	if (blk->node.next != &buffer->blocks)
		next = BLOCK(blk->node.next);

	if (buf[len - 1] != '\n' && next) {
		// must insert to beginning of next block
		return insert_to_next(buf, len);
	}

	if (blk->size + len > BLOCK_EDIT_SIZE) {
		// this block would grow too big, insert to next or add new?
		if (next && len + next->size <= BLOCK_EDIT_SIZE) {
			// fits to next block
			return insert_to_next(buf, len);
		}
		return add_new_block(buf, len);
	}

	// fits to this block
	return append_to_current(buf, len);
}

void do_insert(const char *buf, unsigned int len)
{
	unsigned int nl = insert_bytes(buf, len);

	buffer->nl += nl;
	update_flags |= UPDATE_CURSOR_LINE;
	if (nl)
		update_flags |= UPDATE_FULL;

	update_hl_insert(nl, len);
}

static int only_block(struct block *blk)
{
	return blk->node.prev == &buffer->blocks && blk->node.next == &buffer->blocks;
}

char *do_delete(unsigned int len)
{
	struct list_head *saved_prev_node = NULL;
	struct block *blk = view->cursor.blk;
	unsigned int buffer_nl = buffer->nl;
	unsigned int offset = view->cursor.offset;
	unsigned int pos = 0;
	char *buf;

	if (!len)
		return NULL;

	if (!offset) {
		// the block where cursor is can become empty and thereby may be deleted
		saved_prev_node = blk->node.prev;
	}

	buf = xnew(char, len);
	while (pos < len) {
		struct list_head *next = blk->node.next;
		unsigned int avail = blk->size - offset;
		unsigned int count = len - pos;
		unsigned int nl;

		if (count > avail)
			count = avail;
		nl = copy_count_nl(buf + pos, blk->data + offset, count);
		if (count < avail)
			memmove(blk->data + offset, blk->data + offset + count, avail - count);

		buffer->nl -= nl;
		blk->nl -= nl;
		blk->size -= count;
		if (!blk->size && !only_block(blk))
			delete_block(blk);

		offset = 0;
		pos += count;
		blk = BLOCK(next);

		BUG_ON(pos < len && next == &buffer->blocks);
	}

	if (saved_prev_node)
		view->cursor.blk = BLOCK(saved_prev_node->next);

	blk = view->cursor.blk;
	if (blk->size && blk->data[blk->size - 1] != '\n' && blk->node.next != &buffer->blocks) {
		struct block *next = BLOCK(blk->node.next);
		unsigned int size = blk->size + next->size;

		if (size > blk->alloc) {
			blk->alloc = ALLOC_ROUND(size);
			xrenew(blk->data, blk->alloc);
		}
		memcpy(blk->data + blk->size, next->data, next->size);
		blk->size = size;
		blk->nl += next->nl;
		delete_block(next);
	}

	update_flags |= UPDATE_CURSOR_LINE;
	if (buffer_nl != buffer->nl)
		update_flags |= UPDATE_FULL;

	update_hl_insert(0, -len);
	return buf;
}
