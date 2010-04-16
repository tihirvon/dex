#ifndef CTAGS_H
#define CTAGS_H

#include "ptr-array.h"

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
void search_tags(struct tag_file *tf, struct ptr_array *tags, const char *name);
void free_tags(struct ptr_array *tags);

#endif
