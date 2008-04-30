#ifndef UTIL_H
#define UTIL_H

#include "xmalloc.h"

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
#include <ctype.h>
#include <pwd.h>

#ifndef DEBUG
#define DEBUG 1
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

extern char *home_dir;

void init_misc(void);
const char *editor_file(const char *name);
unsigned int count_nl(const char *buf, unsigned int size);
unsigned int copy_count_nl(char *dst, const char *src, unsigned int len);
ssize_t xread(int fd, void *buf, size_t count);
ssize_t xwrite(int fd, const void *buf, size_t count);
char *path_absolute(const char *filename);
const char *get_home_dir(const char *username, int len);

void ui_start(void);
void ui_end(void);
void any_key(void);
void update_everything(void);

void bug(const char *function, const char *fmt, ...) __FORMAT(2, 3) __NORETURN;
void debug_print(const char *function, const char *fmt, ...) __FORMAT(2, 3);

#define mmap_empty ((void *)8UL)

static inline void *xmmap(int fd, off_t offset, size_t len)
{
	void *buf;
	if (!len)
		return mmap_empty;
	buf = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, offset);
	if (buf == MAP_FAILED)
		return NULL;
	return buf;
}

static inline void xmunmap(void *start, size_t len)
{
	if (start != mmap_empty) {
		BUG_ON(munmap(start, len));
	} else {
		BUG_ON(len);
	}
}

struct wbuf {
	int fill;
	int fd;
	char buf[8192];
};

#define WBUF(name) struct wbuf name = { .fill = 0, .fd = -1, }

int wbuf_flush(struct wbuf *wbuf);
int wbuf_write_str(struct wbuf *wbuf, const char *str);
int wbuf_write_ch(struct wbuf *wbuf, char ch);

#endif
