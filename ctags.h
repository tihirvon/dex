#ifndef CTAGS_H
#define CTAGS_H

#include "libc.h"

struct tag_file {
	char *map;
	long size;
	time_t mtime;
};

struct tag {
	char *name;
	char *filename;
	char *pattern;
	char *member;
	char *typeref;
	int line;
	char kind;
	bool local;
};

struct tag_file *open_tag_file(const char *filename);
void close_tag_file(struct tag_file *tf);
bool next_tag(struct tag_file *tf, size_t *posp, const char *prefix, int exact, struct tag *t);
void free_tag(struct tag *t);

#endif
