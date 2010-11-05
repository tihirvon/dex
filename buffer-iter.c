#include "buffer.h"

unsigned int buffer_get_char(struct block_iter *bi, uchar *up)
{
	struct block_iter tmp = *bi;
	return buffer_next_char(&tmp, up);
}

unsigned int buffer_next_char(struct block_iter *bi, uchar *up)
{
	unsigned int offset = bi->offset;

	if (offset == bi->blk->size) {
		if (bi->blk->node.next == bi->head)
			return 0;
		bi->blk = BLOCK(bi->blk->node.next);
		bi->offset = offset = 0;
	}

	// Note: this block can't be empty
	*up = ((unsigned char *)bi->blk->data)[offset];
	if (*up < 0x80 || !buffer->options.utf8) {
		bi->offset++;
		return 1;
	}

	*up = u_buf_get_char(bi->blk->data, bi->blk->size, &bi->offset);
	return bi->offset - offset;
}

unsigned int buffer_prev_char(struct block_iter *bi, uchar *up)
{
	unsigned int offset = bi->offset;

	if (!offset) {
		if (bi->blk->node.prev == bi->head)
			return 0;
		bi->blk = BLOCK(bi->blk->node.prev);
		bi->offset = offset = bi->blk->size;
	}

	// Note: this block can't be empty
	*up = ((unsigned char *)bi->blk->data)[offset - 1];
	if (*up < 0x80 || !buffer->options.utf8) {
		bi->offset--;
		return 1;
	}

	*up = u_prev_char(bi->blk->data, &bi->offset);
	return offset - bi->offset;
}
