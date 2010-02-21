#ifndef ITER_H
#define ITER_H

#include "common.h"
#include "list.h"
#include "uchar.h"

struct block {
	struct list_head node;
	char *data;
	unsigned int size;
	unsigned int alloc;
	unsigned int nl;
};

#define BLOCK_MAX_SIZE 512

static inline size_t ALLOC_ROUND(size_t size)
{
	return ROUND_UP(size, 64);
}

static inline struct block *BLOCK(struct list_head *item)
{
	return container_of(item, struct block, node);
}

struct block *block_new(int size);
void delete_block(struct block *blk);
unsigned int count_nl(const char *buf, unsigned int size);
unsigned int copy_count_nl(char *dst, const char *src, unsigned int len);

struct block_iter {
	struct block *blk;
	struct list_head *head;
	unsigned int offset;
};

unsigned int block_iter_next_byte(struct block_iter *i, uchar *byte);
unsigned int block_iter_prev_byte(struct block_iter *i, uchar *byte);
unsigned int block_iter_next_uchar(struct block_iter *i, uchar *up);
unsigned int block_iter_prev_uchar(struct block_iter *i, uchar *up);
unsigned int block_iter_next_line(struct block_iter *bi);
unsigned int block_iter_prev_line(struct block_iter *bi);
unsigned int block_iter_bol(struct block_iter *bi);
unsigned int block_iter_eol(struct block_iter *bi);
void block_iter_skip_bytes(struct block_iter *bi, unsigned int count);
void block_iter_goto_offset(struct block_iter *bi, unsigned int offset);
unsigned int block_iter_get_offset(struct block_iter *bi);

static inline int block_iter_eof(struct block_iter *bi)
{
	return bi->offset == bi->blk->size && bi->blk->node.next == bi->head;
}

#endif
