#include "gbuf.h"
#include "xmalloc.h"

void gbuf_resize(struct growing_buffer *buf, size_t size)
{
	size_t align = 16 - 1;

	buf->alloc = (size + align) & ~align;
	buf->buffer = xrealloc(buf->buffer, buf->alloc);
}

void gbuf_free(struct growing_buffer *buf)
{
	free(buf->buffer);
	buf->buffer = NULL;
	buf->alloc = 0;
	buf->count = 0;
}

void gbuf_add_ch(struct growing_buffer *buf, char ch)
{
	size_t avail = gbuf_avail(buf);

	if (avail < 1)
		gbuf_resize(buf, buf->count + 1);
	buf->buffer[buf->count++] = ch;
}

char *gbuf_steal(struct growing_buffer *buf)
{
	char *b;

	gbuf_add_ch(buf, 0);
	b = buf->buffer;
	buf->buffer = NULL;
	buf->alloc = 0;
	buf->count = 0;
	return b;
}
