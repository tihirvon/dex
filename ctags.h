#ifndef CTAGS_H
#define CTAGS_H

#include <stdlib.h>

struct tag_file {
	char *map;
	size_t size;
	time_t mtime;
	int fd;
};

struct tag {
	char *name;
	char *filename;
	char *pattern;
	char *member;
	char *typeref;
	int line;
	char kind;
	char local;
};

struct tag_file *open_tag_file(const char *filename);
void close_tag_file(struct tag_file *tf);
int next_tag(struct tag_file *tf, size_t *posp, const char *prefix, int exact, struct tag *t);
void free_tag(struct tag *t);

#endif
