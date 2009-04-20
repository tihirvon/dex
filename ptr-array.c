#include "ptr-array.h"
#include "xmalloc.h"

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

void ptr_array_free(struct ptr_array *array)
{
	int i;

	for (i = 0; i < array->count; i++)
		free(array->ptrs[i]);
	free(array->ptrs);
}
