/*
 * This code is largely based on strbuf in the GIT version control system.
 */

#include "gbuf.h"
#include "common.h"

unsigned char gbuf_empty_buffer[1];

void gbuf_grow(struct gbuf *buf, size_t more)
{
	size_t alloc = ROUND_UP(buf->len + more + 1, 16);

	if (alloc > buf->alloc) {
		if (!buf->alloc)
			buf->buffer = NULL;
		buf->alloc = alloc;
		buf->buffer = xrealloc(buf->buffer, buf->alloc);
		// gbuf is not NUL terminated if this was first alloc
		buf->buffer[buf->len] = 0;
	}
}

void gbuf_free(struct gbuf *buf)
{
	if (buf->alloc)
		free(buf->buffer);
	gbuf_init(buf);
}

void gbuf_add_ch(struct gbuf *buf, char ch)
{
	gbuf_grow(buf, 1);
	buf->buffer[buf->len++] = ch;
	buf->buffer[buf->len] = 0;
}

void gbuf_add_str(struct gbuf *buf, const char *str)
{
	int len = strlen(str);

	if (!len)
		return;
	gbuf_grow(buf, len);
	memcpy(buf->buffer + buf->len, str, len + 1);
	buf->len += len;
}

void gbuf_add_buf(struct gbuf *buf, const char *ptr, size_t len)
{
	if (!len)
		return;
	gbuf_grow(buf, len);
	memcpy(buf->buffer + buf->len, ptr, len);
	buf->len += len;
	buf->buffer[buf->len] = 0;
}

char *gbuf_steal(struct gbuf *buf)
{
	char *b = buf->buffer;
	if (!buf->alloc)
		b = xcalloc(1);
	gbuf_init(buf);
	return b;
}

void gbuf_make_space(struct gbuf *buf, size_t pos, size_t len)
{
	BUG_ON(pos > buf->len);
	gbuf_grow(buf, len);
	memmove(buf->buffer + pos + len, buf->buffer + pos, buf->len - pos);
	buf->len += len;
	buf->buffer[buf->len] = 0;
}

void gbuf_remove(struct gbuf *buf, size_t pos, size_t len)
{
	BUG_ON(pos + len > buf->len);
	memmove(buf->buffer + pos, buf->buffer + pos + len, buf->len - pos - len);
	buf->len -= len;
	buf->buffer[buf->len] = 0;
}
