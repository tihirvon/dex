#ifndef COMMON_H
#define COMMON_H

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <limits.h>
#include <dirent.h>
#include <pwd.h>
#include <regex.h>
#include <time.h>
#include <wctype.h>
#include <signal.h>
#include <iconv.h>

#include "ctype.h"

extern const char hex_tab[16];
extern int term_utf8;

#if defined(__GNUC__)

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#define __NORETURN	__attribute__((__noreturn__))

/* Argument at index @fmt_idx is printf compatible format string and
 * argument at index @first_idx is the first format argument */
#define __FORMAT(fmt_idx, first_idx) __attribute__((format(printf, (fmt_idx), (first_idx))))

#else

#define likely(x)	(x)
#define unlikely(x)	(x)
#define __NORETURN
#define __FORMAT(fmt_idx, first_idx)

#endif

#define ARRAY_COUNT(x) ((unsigned long)sizeof(x) / sizeof(x[0]))
#define clear(ptr) memset((ptr), 0, sizeof(*(ptr)))

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

#define __STR(a) #a
#define BUG_ON(a) \
	do { \
		if (unlikely(a)) \
			BUG("%s\n", __STR(a)); \
	} while (0)

static inline int str_has_prefix(const char *str, const char *prefix)
{
	return !strncmp(str, prefix, strlen(prefix));
}

int count_strings(char **strings);
unsigned int number_width(unsigned int n);
int buf_parse_long(const char *str, int size, int *posp, long *valp);
int parse_long(const char **strp, long *valp);
char *xsprintf(const char *format, ...) __FORMAT(1, 2);
ssize_t xread(int fd, void *buf, size_t count);
ssize_t xwrite(int fd, const void *buf, size_t count);
ssize_t read_file(const char *filename, char **bufp);
char *buf_next_line(char *buf, ssize_t *posp, ssize_t size);
void bug(const char *function, const char *fmt, ...) __FORMAT(2, 3) __NORETURN;
void debug_print(const char *function, const char *fmt, ...) __FORMAT(2, 3);
void *xmmap(int fd, off_t offset, size_t len);
void xmunmap(void *start, size_t len);

#include "xmalloc.h"

#endif
