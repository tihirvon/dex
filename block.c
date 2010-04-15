#include "block.h"

struct block *block_new(unsigned int alloc)
{
	struct block *blk = xnew0(struct block, 1);

	alloc = ALLOC_ROUND(alloc);
	blk->data = xnew(char, alloc);
	blk->alloc = alloc;
	return blk;
}

void delete_block(struct block *blk)
{
	list_del(&blk->node);
	free(blk->data);
	free(blk);
}

unsigned int copy_count_nl(char *dst, const char *src, unsigned int len)
{
	unsigned int i, nl = 0;
	for (i = 0; i < len; i++) {
		dst[i] = src[i];
		if (src[i] == '\n')
			nl++;
	}
	return nl;
}
