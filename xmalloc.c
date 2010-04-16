#include "xmalloc.h"
#include "common.h"

static void __NORETURN malloc_fail(void)
{
	fprintf(stderr, "out of memory\n");
	exit(42);
}

void *xmalloc(size_t size)
{
	void *ptr = malloc(size);

	if (unlikely(ptr == NULL))
		malloc_fail();
	return ptr;
}

void *xcalloc(size_t size)
{
	void *ptr = calloc(1, size);

	if (unlikely(ptr == NULL))
		malloc_fail();
	return ptr;
}

void *xrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (unlikely(ptr == NULL))
		malloc_fail();
	return ptr;
}

char *xstrdup(const char *str)
{
	char *s = strdup(str);

	if (unlikely(s == NULL))
		malloc_fail();
	return s;
}

void *xmemdup(const void *ptr, size_t size)
{
	void *buf = xmalloc(size);
	memcpy(buf, ptr, size);
	return buf;
}

char *xstrndup(const char *str, size_t n)
{
	int len;
	char *s;

	for (len = 0; len < n && str[len]; len++)
		;
	s = xmalloc(len + 1);
	memcpy(s, str, len);
	s[len] = 0;
	return s;
}
