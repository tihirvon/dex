#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
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

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#define __NORETURN	__attribute__((__noreturn__))

/* Argument at index @fmt_idx is printf compatible format string and
 * argument at index @first_idx is the first format argument */
#define __FORMAT(fmt_idx, first_idx) __attribute__((format(printf, (fmt_idx), (first_idx))))

#define ARRAY_COUNT(x) (sizeof(x) / sizeof(x[0]))

#define ROUND_UP(x, r) (((x) + r - 1) & ~(r - 1))

#ifndef DEBUG
#define DEBUG 1
#endif

#ifndef DEBUG_SYNTAX
#define DEBUG_SYNTAX DEBUG
#endif

#if DEBUG <= 0
#define BUG(...) do { } while (0)
#define d_print(...) do { } while (0)
#else
#define BUG(...) bug(__FUNCTION__, __VA_ARGS__)
#define d_print(...) debug_print(__FUNCTION__, __VA_ARGS__)
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
