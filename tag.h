#ifndef TAG_H
#define TAG_H

#include "ctags.h"
#include "ptr-array.h"

void free_tags(struct ptr_array *tags);
int find_tags(const char *name, struct ptr_array *tags);
void collect_tags(const char *prefix);

#endif
