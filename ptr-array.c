#include "ptr-array.h"
#include "xmalloc.h"

#include <string.h>

void ptr_array_add(struct ptr_array *array, void *ptr)
{
	if (array->count == array->alloc) {
		array->alloc *= 3;
		array->alloc /= 2;
		if (!array->alloc)
			array->alloc = 8;
		xrenew(array->ptrs, array->alloc);
	}
	array->ptrs[array->count++] = ptr;
}

void ptr_array_insert(struct ptr_array *array, void *ptr, long pos)
{
	long count = array->count - pos;
	ptr_array_add(array, NULL);
	memmove(array->ptrs + pos + 1, array->ptrs + pos, count * sizeof(void *));
	array->ptrs[pos] = ptr;
}

void ptr_array_free(struct ptr_array *array)
{
	int i;

	for (i = 0; i < array->count; i++)
		free(array->ptrs[i]);
	free(array->ptrs);
}

void ptr_array_remove(struct ptr_array *array, void *ptr)
{
	long pos = ptr_array_idx(array, ptr);
	ptr_array_remove_idx(array, pos);
}

void *ptr_array_remove_idx(struct ptr_array *array, long pos)
{
	void *ptr = array->ptrs[pos];
	array->count--;
	memmove(array->ptrs + pos, array->ptrs + pos + 1, (array->count - pos) * sizeof(void *));
	return ptr;
}

long ptr_array_idx(struct ptr_array *array, void *ptr)
{
	long i;

	for (i = 0; i < array->count; i++) {
		if (array->ptrs[i] == ptr)
			return i;
	}
	return -1;
}
