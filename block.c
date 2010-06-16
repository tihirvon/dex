#include "block.h"
#include "buffer.h"

#define BLOCK_EDIT_SIZE 512

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

/*
 * Combine current block and new data into smaller blocks:
 *   - Block _must_ contain whole lines
 *   - Block _must_ contain at least one line
 *   - Preferred maximum size of block is BLOCK_EDIT_SIZE
 *   - Size of any block can be larger than BLOCK_EDIT_SIZE
 *     only if there's a very long line
 */
static unsigned int split_and_insert(const char *buf, unsigned int len)
{
	struct block *blk = view->cursor.blk;
	struct list_head *prev_node = blk->node.prev;
	const char *buf1 = blk->data;
	const char *buf2 = buf;
	const char *buf3 = blk->data + view->cursor.offset;
	unsigned int size1 = view->cursor.offset;
	unsigned int size2 = len;
	unsigned int size3 = blk->size - size1;
	unsigned int total = size1 + size2 + size3;
	unsigned int start = 0; // beginning of new block
	unsigned int size = 0;  // size of new block
	unsigned int pos = 0;   // current position
	unsigned int nl_added = 0;

	while (start < total) {
		// size of new block if next line would be added
		unsigned int new_size = 0;
		unsigned int copied = 0;
		struct block *new;

		if (pos < size1) {
			const char *nl = memchr(buf1 + pos, '\n', size1 - pos);
			if (nl)
				new_size = nl - buf1 + 1 - start;
		}

		if (!new_size && pos < size1 + size2) {
			unsigned int offset = 0;
			const char *nl;

			if (pos > size1)
				offset = pos - size1;

			nl = memchr(buf2 + offset, '\n', size2 - offset);
			if (nl)
				new_size = size1 + nl - buf2 + 1 - start;
		}

		if (!new_size && pos < total) {
			unsigned int offset = 0;
			const char *nl;

			if (pos > size1 + size2)
				offset = pos - size1 - size2;

			nl = memchr(buf3 + offset, '\n', size3 - offset);
			if (nl)
				new_size = size1 + size2 + nl - buf3 + 1 - start;
			else
				new_size = total - start;
		}

		if (new_size <= BLOCK_EDIT_SIZE) {
			// fits
			size = new_size;
			pos = start + new_size;
			if (pos < total)
				continue;
		} else {
			// does not fit
			if (!size) {
				// one block containing one very long line
				size = new_size;
				pos = start + new_size;
			}
		}

		BUG_ON(!size);
		new = block_new(size);
		if (start < size1) {
			unsigned int avail = size1 - start;
			unsigned int count = size;

			if (count > avail)
				count = avail;
			new->nl += copy_count_nl(new->data, buf1 + start, count);
			copied += count;
			start += count;
		}
		if (start >= size1 && start < size1 + size2) {
			unsigned int offset = start - size1;
			unsigned int avail = size2 - offset;
			unsigned int count = size - copied;

			if (count > avail)
				count = avail;
			new->nl += copy_count_nl(new->data + copied, buf2 + offset, count);
			copied += count;
			start += count;
		}
		if (start >= size1 + size2) {
			unsigned int offset = start - size1 - size2;
			unsigned int avail = size3 - offset;
			unsigned int count = size - copied;

			BUG_ON(count > avail);
			new->nl += copy_count_nl(new->data + copied, buf3 + offset, count);
			copied += count;
			start += count;
		}

		new->size = size;
		BUG_ON(copied != size);
		list_add_before(&new->node, &blk->node);

		nl_added += new->nl;
		size = 0;
	}

	view->cursor.blk = BLOCK(prev_node->next);
	while (view->cursor.offset > view->cursor.blk->size) {
		view->cursor.offset -= view->cursor.blk->size;
		view->cursor.blk = BLOCK(view->cursor.blk->node.next);
	}

	nl_added -= blk->nl;
	delete_block(blk);
	return nl_added;
}

static unsigned int insert_bytes(const char *buf, unsigned int len)
{
	struct block *blk = view->cursor.blk;
	unsigned int new_size = blk->size + len;

	// never append in case last character of buf is not newline
	if (view->cursor.offset == blk->size && blk->node.next != &buffer->blocks) {
		blk = BLOCK(blk->node.next);
		view->cursor.blk = blk;
		view->cursor.offset = 0;
	}

	if (new_size <= blk->alloc || new_size <= BLOCK_EDIT_SIZE)
		return insert_to_current(buf, len);
	return split_and_insert(buf, len);
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
