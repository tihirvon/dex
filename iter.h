#ifndef ITER_H
#define ITER_H

#include "uchar.h"
#include "list.h"

struct block {
	struct list_head node;
	char *data;
	unsigned int size;
	unsigned int alloc;
	unsigned int nl;
};

static inline struct block *BLOCK(struct list_head *item)
{
	return container_of(item, struct block, node);
}

struct block_iter {
	struct block *blk;
	struct list_head *head;
	unsigned int offset;
};

struct lineref {
	const char *line;
	unsigned int size;
};

void block_iter_normalize(struct block_iter *bi);
unsigned int block_iter_next_byte(struct block_iter *i, uchar *byte);
unsigned int block_iter_prev_byte(struct block_iter *i, uchar *byte);
unsigned int block_iter_next_line(struct block_iter *bi);
unsigned int block_iter_prev_line(struct block_iter *bi);
unsigned int block_iter_bol(struct block_iter *bi);
unsigned int block_iter_eol(struct block_iter *bi);
void block_iter_skip_bytes(struct block_iter *bi, unsigned int count);
void block_iter_goto_offset(struct block_iter *bi, unsigned int offset);
unsigned int block_iter_get_offset(const struct block_iter *bi);

static inline int block_iter_eof(struct block_iter *bi)
{
	return bi->offset == bi->blk->size && bi->blk->node.next == bi->head;
}

void fill_line_ref(struct block_iter *bi, struct lineref *lr);
void fill_line_nl_ref(struct block_iter *bi, struct lineref *lr);
unsigned int fetch_this_line(const struct block_iter *bi, struct lineref *lr);

#endif
