#ifndef GBUF_H
#define GBUF_H

#include <stdlib.h>

struct growing_buffer {
	char *buffer;
	size_t alloc;
	size_t count;
};

#define GROWING_BUFFER(name) struct growing_buffer name = { NULL, 0, 0 };

static inline size_t gbuf_avail(struct growing_buffer *buf)
{
	return buf->alloc - buf->count;
}

void gbuf_resize(struct growing_buffer *buf, size_t size);
void gbuf_free(struct growing_buffer *buf);
void gbuf_add_ch(struct growing_buffer *buf, char ch);
char *gbuf_steal(struct growing_buffer *buf);

#endif
