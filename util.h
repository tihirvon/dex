#ifndef UTIL_H
#define UTIL_H

#include "xmalloc.h"
#include "debug.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
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

extern char *home_dir;

void init_misc(void);
unsigned int count_nl(const char *buf, unsigned int size);
unsigned int copy_count_nl(char *dst, const char *src, unsigned int len);
ssize_t xread(int fd, void *buf, size_t count);
ssize_t xwrite(int fd, const void *buf, size_t count);
char *path_absolute(const char *filename);
void ui_end(void);

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

#endif
