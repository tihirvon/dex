#include "file-location.h"
#include "common.h"

void file_location_free(struct file_location *loc)
{
	free(loc->filename);
	free(loc->pattern);
	free(loc);
}

bool file_location_equals(const struct file_location *a, const struct file_location *b)
{
	if (!xstreq(a->filename, b->filename)) {
		return false;
	}
	if (a->buffer_id != b->buffer_id) {
		return false;
	}
	if (!xstreq(a->pattern, b->pattern)) {
		return false;
	}
	if (a->line != b->line) {
		return false;
	}
	if (a->column != b->column) {
		return false;
	}
	return true;
}
