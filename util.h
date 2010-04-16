#ifndef UTIL_H
#define UTIL_H

#include <stdlib.h>

ssize_t xread(int fd, void *buf, size_t count);
ssize_t xwrite(int fd, const void *buf, size_t count);
char *path_absolute(const char *filename);
const char *get_file_type(mode_t mode);
void *xmmap(int fd, off_t offset, size_t len);
void xmunmap(void *start, size_t len);

#endif
