#ifndef PTR_ARRAY_H
#define PTR_ARRAY_H

struct ptr_array {
	void **ptrs;
	unsigned int alloc;
	unsigned int count;
};

#define PTR_ARRAY(name) struct ptr_array name = { NULL, 0, 0 }

void ptr_array_add(struct ptr_array *array, void *ptr);
void ptr_array_free(struct ptr_array *array);

#endif
