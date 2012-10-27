#ifndef PTR_ARRAY_H
#define PTR_ARRAY_H

#include <stdlib.h>

struct ptr_array {
	void **ptrs;
	long alloc;
	long count;
};

#define PTR_ARRAY(name) struct ptr_array name = { NULL, 0, 0 }

void ptr_array_add(struct ptr_array *array, void *ptr);
void ptr_array_insert(struct ptr_array *array, void *ptr, long pos);
void ptr_array_free(struct ptr_array *array);
void *ptr_array_remove(struct ptr_array *array, long pos);
int ptr_array_idx(struct ptr_array *array, void *ptr);

#endif
