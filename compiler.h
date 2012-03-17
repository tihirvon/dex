#ifndef COMPILER_H
#define COMPILER_H

#include "ptr-array.h"

struct compile_error {
	char *file;
	char *msg;
	int line;
	int column;
};

struct error_format {
	int ignore;
	signed char msg_idx;
	signed char file_idx;
	signed char line_idx;
	signed char column_idx;
	const char *pattern;
};

struct compiler {
	char *name;
	struct ptr_array error_formats;
};

void add_error_fmt(const char *compiler, int ignore, const char *format, char **desc);
struct compiler *find_compiler(const char *name);

#endif
