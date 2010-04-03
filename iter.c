#include "iter.h"

static void block_iter_normalize(struct block_iter *bi)
{
	struct block *blk = bi->blk;

	if (bi->offset == blk->size && blk->node.next != bi->head) {
		bi->blk = BLOCK(blk->node.next);
		bi->offset = 0;
	}
}

// analogous to *ptr++
unsigned int block_iter_next_byte(struct block_iter *i, uchar *byte)
{
	if (i->offset == i->blk->size) {
		if (i->blk->node.next == i->head)
			return 0;
		i->blk = BLOCK(i->blk->node.next);
		i->offset = 0;
	}
	*byte = (unsigned char)i->blk->data[i->offset];
	i->offset++;
	return 1;
}

// analogous to *--ptr
unsigned int block_iter_prev_byte(struct block_iter *i, uchar *byte)
{
	if (!i->offset) {
		if (i->blk->node.prev == i->head)
			return 0;
		i->blk = BLOCK(i->blk->node.prev);
		i->offset = i->blk->size;
	}
	i->offset--;
	*byte = (unsigned char)i->blk->data[i->offset];
	return 1;
}

// analogous to *ptr++
unsigned int block_iter_next_uchar(struct block_iter *i, uchar *up)
{
	struct block_iter save;
	int c, len;
	uchar ch, u;

	if (!block_iter_next_byte(i, &ch))
		return 0;

	*up = ch;
	if (likely(ch < 0x80)) {
		// ascii
		return 1;
	}

	*up = ch | U_INVALID_MASK;
	len = u_len_tab[ch];
	if (len < 1) {
		// invalid first byte
		return 1;
	}

	save = *i;
	u = ch & u_first_byte_mask[len - 1];
	for (c = 1; c < len; c++) {
		if (!block_iter_next_byte(i, &ch))
			goto crap;
		if (u_len_tab[ch])
			goto crap;

		u = (u << 6) | (ch & 0x3f);
	}
	if (u < u_min_val[len - 1] || u > u_max_val[len - 1])
		goto crap;
	*up = u;
	return len;
crap:
	// *up set to the first byte and marked invalid
	*i = save;
	return 1;
}

// analogous to *--ptr
unsigned int block_iter_prev_uchar(struct block_iter *i, uchar *up)
{
	struct block_iter save;
	int c, len;
	uchar ch, u;
	unsigned int shift;

	if (!block_iter_prev_byte(i, &ch))
		return 0;

	*up = ch;
	if (likely(ch < 0x80)) {
		// ascii
		return 1;
	}

	*up = ch | U_INVALID_MASK;
	save = *i;
	u = 0;
	shift = 0;
	for (c = 1; c < 4; c++) {
		len = u_len_tab[ch];
		if (len)
			break;

		u |= (ch & 0x3f) << shift;
		shift += 6;
		if (!block_iter_prev_byte(i, &ch))
			goto crap;
	}
	if (len != c)
		goto crap;
	u |= (ch & u_first_byte_mask[len - 1]) << shift;
	if (u < u_min_val[len - 1] || u > u_max_val[len - 1])
		goto crap;
	*up = u;
	return len;
crap:
	// *up set to the first byte we read and marked invalid
	*i = save;
	return 1;
}

unsigned int block_iter_next_line(struct block_iter *bi)
{
	unsigned int count = 0;

	while (1) {
		uchar u;

		if (!block_iter_next_byte(bi, &u))
			return 0;
		count++;
		if (u == '\n')
			return count;
	}
}

/*
 * Move to beginning of previous line.
 * Returns number of bytes moved which is zero if there's no previous line.
 */
unsigned int block_iter_prev_line(struct block_iter *bi)
{
	struct block *blk = bi->blk;
	unsigned int offset = bi->offset;
	unsigned int start = offset;

	while (offset && blk->data[offset - 1] != '\n')
		offset--;

	if (!offset) {
		if (blk->node.prev == bi->head)
			return 0;
		bi->blk = blk = BLOCK(blk->node.prev);
		offset = blk->size;
		start += offset;
	}

	offset--;
	while (offset && blk->data[offset - 1] != '\n')
		offset--;
	bi->offset = offset;
	return start - offset;
}

unsigned int block_iter_bol(struct block_iter *bi)
{
	unsigned int count = 0;

	while (1) {
		uchar u;

		if (!block_iter_prev_byte(bi, &u))
			break;
		if (u == '\n') {
			block_iter_next_byte(bi, &u);
			break;
		}
		count++;
	}
	return count;
}

unsigned int block_iter_eol(struct block_iter *bi)
{
	unsigned int count = 0;

	while (1) {
		uchar u;

		if (!block_iter_next_byte(bi, &u))
			break;
		if (u == '\n') {
			block_iter_prev_byte(bi, &u);
			break;
		}
		count++;
	}
	return count;
}

void block_iter_skip_bytes(struct block_iter *bi, unsigned int count)
{
	struct block *blk = bi->blk;
	unsigned int offset = bi->offset;

	while (1) {
		unsigned int avail = blk->size - offset;

		if (count <= avail) {
			bi->blk = blk;
			bi->offset = offset + count;
			return;
		}
		blk = BLOCK(blk->node.next);
		count -= avail;
		offset = 0;
	}
}

void block_iter_goto_offset(struct block_iter *bi, unsigned int offset)
{
	struct block *blk;

	list_for_each_entry(blk, bi->head, node) {
		if (offset <= blk->size) {
			bi->blk = blk;
			bi->offset = offset;
			return;
		}
		offset -= blk->size;
	}
}

unsigned int block_iter_get_offset(struct block_iter *bi)
{
	struct block *blk;
	unsigned int offset = 0;

	list_for_each_entry(blk, bi->head, node) {
		if (blk == bi->blk)
			break;
		offset += blk->size;
	}
	return offset + bi->offset;
}

// bi should be at bol
void fill_line_ref(struct block_iter *bi, struct lineref *lr)
{
	unsigned int max;
	const char *ptr, *nl;

	block_iter_normalize(bi);

	max = bi->blk->size - bi->offset;
	ptr = bi->blk->data + bi->offset;
	nl = memchr(ptr, '\n', max);

	lr->line = ptr;
	lr->size = nl ? nl - ptr : max;
}

void fill_line_nl_ref(struct block_iter *bi, struct lineref *lr)
{
	unsigned int max;
	const char *ptr, *nl;

	block_iter_normalize(bi);

	max = bi->blk->size - bi->offset;
	ptr = bi->blk->data + bi->offset;
	nl = memchr(ptr, '\n', max);

	lr->line = ptr;
	lr->size = nl ? nl - ptr + 1 : max;
}

unsigned int fetch_this_line(const struct block_iter *bi, struct lineref *lr)
{
	struct block_iter tmp = *bi;
	unsigned int count = block_iter_bol(&tmp);

	fill_line_ref(&tmp, lr);
	return count;
}
