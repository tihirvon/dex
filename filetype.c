#include "filetype.h"
#include "common.h"
#include "regexp.h"
#include "ptr-array.h"

struct filetype {
	char *name;
	char *str;
	enum detect_type type;
};

static PTR_ARRAY(filetypes);

void add_filetype(const char *name, const char *str, enum detect_type type)
{
	struct filetype *ft;

	ft = xnew(struct filetype, 1);
	ft->name = xstrdup(name);
	ft->str = xstrdup(str);
	ft->type = type;
	ptr_array_add(&filetypes, ft);
}

const char *find_ft(const char *filename, const char *interpreter,
	const char *first_line, unsigned int line_len)
{
	unsigned int filename_len = strlen(filename);
	const char *ext = NULL;
	int i;

	if (filename)
		ext = strrchr(filename, '.');
	if (ext)
		ext++;
	for (i = 0; i < filetypes.count; i++) {
		const struct filetype *ft = filetypes.ptrs[i];

		switch (ft->type) {
		case FT_EXTENSION:
			if (!ext || strcmp(ext, ft->str))
				continue;
			break;
		case FT_FILENAME:
			if (!filename || !regexp_match_nosub(ft->str, filename, filename_len))
				continue;
			break;
		case FT_CONTENT:
			if (!first_line || !regexp_match_nosub(ft->str, first_line, line_len))
				continue;
			break;
		case FT_INTERPRETER:
			if (!interpreter || strcmp(interpreter, ft->str))
				continue;
			break;
		}
		return ft->name;
	}
	return NULL;
}

int is_ft(const char *name)
{
	int i;

	for (i = 0; i < filetypes.count; i++) {
		const struct filetype *ft = filetypes.ptrs[i];
		if (!strcmp(ft->name, name))
			return 1;
	}
	return 0;
}
