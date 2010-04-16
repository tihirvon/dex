#ifndef COMMON_H
#define COMMON_H

#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <limits.h>
#include <dirent.h>
#include <ctype.h>
#include <pwd.h>
#include <regex.h>
#include <time.h>

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#define __NORETURN	__attribute__((__noreturn__))

/* Argument at index @fmt_idx is printf compatible format string and
 * argument at index @first_idx is the first format argument */
#define __FORMAT(fmt_idx, first_idx) __attribute__((format(printf, (fmt_idx), (first_idx))))

#define ARRAY_COUNT(x) (sizeof(x) / sizeof(x[0]))

static inline size_t ROUND_UP(size_t x, size_t r)
{
	r--;
	return (x + r) & ~r;
}

#if DEBUG <= 0
__FORMAT(1, 2)
static inline void BUG(const char *fmt, ...)
{
}
#else
#define BUG(...) bug(__FUNCTION__, __VA_ARGS__)
#endif

#if DEBUG <= 1
__FORMAT(1, 2)
static inline void d_print(const char *fmt, ...)
{
}
#else
#define d_print(...) debug_print(__FUNCTION__, __VA_ARGS__)
#endif

#if DEBUG_SYNTAX <= 0
__FORMAT(1, 2)
static inline void ds_print(const char *fmt, ...)
{
}
#else
#define ds_print(...) debug_print(__FUNCTION__, __VA_ARGS__)
#endif

#define __STR(a) #a
#define BUG_ON(a) \
	do { \
		if (unlikely(a)) \
			BUG("%s\n", __STR(a)); \
	} while (0)

void bug(const char *function, const char *fmt, ...) __FORMAT(2, 3) __NORETURN;
void debug_print(const char *function, const char *fmt, ...) __FORMAT(2, 3);

#include "xmalloc.h"

#endif
