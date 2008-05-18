#ifndef GBUF_H
#define GBUF_H

#include "common.h"

struct gbuf {
	char *buffer;
	size_t alloc;
	size_t len;
};

extern char gbuf_empty_buffer[];

#define GBUF(name) struct gbuf name = { gbuf_empty_buffer, 0, 0 }

static inline void gbuf_truncate(struct gbuf *buf, size_t pos)
{
	BUG_ON(pos > buf->len);
	buf->len = pos;
	buf->buffer[pos] = 0;
}

static inline void gbuf_clear(struct gbuf *buf)
{
	buf->len = 0;
	buf->buffer[0] = 0;
}

static inline size_t gbuf_avail(struct gbuf *buf)
{
	if (buf->alloc)
		return buf->alloc - buf->len - 1;
	return 0;
}

void gbuf_grow(struct gbuf *buf, size_t more);
void gbuf_free(struct gbuf *buf);
void gbuf_add_ch(struct gbuf *buf, char ch);
void gbuf_add_str(struct gbuf *buf, const char *str);
void gbuf_add_buf(struct gbuf *buf, const char *buffer, size_t len);
char *gbuf_steal(struct gbuf *buf);
void gbuf_make_space(struct gbuf *buf, size_t pos, size_t len);
void gbuf_remove(struct gbuf *buf, size_t pos, size_t len);

#endif
