#ifndef XMALLOC_H
#define XMALLOC_H

#include "common.h"

#if defined(__GNUC__) && (__GNUC__ >= 3)
#define __MALLOC	__attribute__((__malloc__))
#else
#define __MALLOC
#endif

void malloc_fail(void) __NORETURN;

#define xnew(type, n)		(type *)xmalloc(sizeof(type) * (n))
#define xnew0(type, n)		(type *)xcalloc(sizeof(type) * (n))
#define xrenew(mem, n)		do { \
					mem = xrealloc(mem, sizeof(*mem) * (n)); \
				} while (0)

static inline void * __MALLOC xmalloc(size_t size)
{
	void *ptr = malloc(size);

	if (unlikely(ptr == NULL))
		malloc_fail();
	return ptr;
}

static inline void * __MALLOC xcalloc(size_t size)
{
	void *ptr = calloc(1, size);

	if (unlikely(ptr == NULL))
		malloc_fail();
	return ptr;
}

static inline void * __MALLOC xrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (unlikely(ptr == NULL))
		malloc_fail();
	return ptr;
}

static inline char * __MALLOC xstrdup(const char *str)
{
	char *s = strdup(str);

	if (unlikely(s == NULL))
		malloc_fail();
	return s;
}

static inline void * __MALLOC xmemdup(void *ptr, size_t size)
{
	void *buf = xmalloc(size);
	memcpy(buf, ptr, size);
	return buf;
}

char * __MALLOC xstrndup(const char *str, size_t n);

#endif
