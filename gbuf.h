#ifndef GBUF_H
#define GBUF_H

#include <stdlib.h>

struct gbuf {
	unsigned char *buffer;
	size_t alloc;
	size_t len;
};

extern unsigned char gbuf_empty_buffer[];

#define GBUF_INIT { gbuf_empty_buffer, 0, 0 }
#define GBUF(name) struct gbuf name = GBUF_INIT

static inline void gbuf_init(struct gbuf *buf)
{
	buf->buffer = gbuf_empty_buffer;
	buf->alloc = 0;
	buf->len = 0;
}

static inline void gbuf_clear(struct gbuf *buf)
{
	buf->len = 0;
	buf->buffer[0] = 0;
}

void gbuf_grow(struct gbuf *buf, size_t more);
void gbuf_free(struct gbuf *buf);
void gbuf_add_ch(struct gbuf *buf, char ch);
void gbuf_add_str(struct gbuf *buf, const char *str);
void gbuf_add_buf(struct gbuf *buf, const char *ptr, size_t len);
char *gbuf_steal(struct gbuf *buf);
void gbuf_make_space(struct gbuf *buf, size_t pos, size_t len);
void gbuf_remove(struct gbuf *buf, size_t pos, size_t len);

#endif
