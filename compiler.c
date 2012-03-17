#include "compiler.h"
#include "editor.h"

static PTR_ARRAY(compilers);

static struct compiler *add_compiler(const char *name)
{
	struct compiler *c = find_compiler(name);

	if (c)
		return c;

	c = xnew0(struct compiler, 1);
	c->name = xstrdup(name);
	ptr_array_add(&compilers, c);
	return c;
}

struct compiler *find_compiler(const char *name)
{
	int i;

	for (i = 0; i < compilers.count; i++) {
		struct compiler *c = compilers.ptrs[i];
		if (!strcmp(c->name, name))
			return c;
	}
	return NULL;
}

void add_error_fmt(const char *compiler, int ignore, const char *format, char **desc)
{
	const char *names[] = { "file", "line", "column", "message" };
	int idx[ARRAY_COUNT(names)] = { -1, -1, -1, 0 };
	struct error_format *f;
	int i, j;

	for (i = 0; desc[i]; i++) {
		for (j = 0; j < ARRAY_COUNT(names); j++) {
			if (!strcmp(desc[i], names[j])) {
				idx[j] = i + 1;
				break;
			}
		}
		if (j == ARRAY_COUNT(names)) {
			error_msg("Unknown substring name %s.", desc[i]);
			return;
		}
	}

	f = xnew0(struct error_format, 1);
	f->ignore = ignore;
	f->msg_idx = idx[3];
	f->file_idx = idx[0];
	f->line_idx = idx[1];
	f->column_idx = idx[2];
	f->pattern = xstrdup(format);

	ptr_array_add(&add_compiler(compiler)->error_formats, f);
}
