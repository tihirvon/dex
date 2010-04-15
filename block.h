#ifndef BLOCK_H
#define BLOCK_H

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

static inline struct block *BLOCK(struct list_head *item)
{
	return container_of(item, struct block, node);
}

struct block *block_new(unsigned int size);
void do_insert(const char *buf, unsigned int len);
char *do_delete(unsigned int len);

#endif
