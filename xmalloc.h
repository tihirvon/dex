#ifndef XMALLOC_H
#define XMALLOC_H

#include <stdlib.h>
#include <string.h>

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#define __NORETURN	__attribute__((__noreturn__))

/* Argument at index @fmt_idx is printf compatible format string and
 * argument at index @first_idx is the first format argument */
#define __FORMAT(fmt_idx, first_idx) __attribute__((format(printf, (fmt_idx), (first_idx))))

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
	void *dup = xmalloc(size);
	memcpy(dup, ptr, size);
	return dup;
}

char * __MALLOC xstrndup(const char *str, size_t n);

#endif
