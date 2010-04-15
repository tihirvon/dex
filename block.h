#ifndef BLOCK_H
#define BLOCK_H

#include "common.h"
#include "list.h"

struct block {
	struct list_head node;
	char *data;
	unsigned int size;
	unsigned int alloc;
	unsigned int nl;
};

#define BLOCK_INIT_SIZE 8192
#define BLOCK_EDIT_SIZE 512

static inline size_t ALLOC_ROUND(size_t size)
{
	return ROUND_UP(size, 64);
}

static inline struct block *BLOCK(struct list_head *item)
{
	return container_of(item, struct block, node);
}

struct block *block_new(unsigned int size);
void delete_block(struct block *blk);
unsigned int copy_count_nl(char *dst, const char *src, unsigned int len);

#endif
