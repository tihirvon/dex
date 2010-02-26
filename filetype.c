#include "filetype.h"
#include "common.h"
#include "util.h"
#include "ptr-array.h"

struct filetype {
	char *name;
	char *str;
	enum {
		FT_EXTENSION,
		FT_FILENAME,
		FT_CONTENT,
	} type;
};

static PTR_ARRAY(filetypes);

static void add_filetype(const char *name, const char *str, int type)
{
	struct filetype *ft;

	ft = xnew(struct filetype, 1);
	ft->name = xstrdup(name);
	ft->str = xstrdup(str);
	ft->type = type;
	ptr_array_add(&filetypes, ft);
}

void add_ft_extensions(const char *name, char * const *extensions)
{
	int i;
	for (i = 0; extensions[i]; i++)
		add_filetype(name, extensions[i], FT_EXTENSION);
}

void add_ft_match(const char *name, const char *pattern)
{
	add_filetype(name, pattern, FT_FILENAME);
}

void add_ft_content(const char *name, const char *pattern)
{
	add_filetype(name, pattern, FT_CONTENT);
}

const char *find_ft(const char *filename, const char *first_line, unsigned int line_len)
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
			if (ext && !strcmp(ext, ft->str))
				return ft->name;
			break;
		case FT_FILENAME:
			if (filename && regexp_match_nosub(ft->str, filename, filename_len))
				return ft->name;
			break;
		case FT_CONTENT:
			if (first_line && regexp_match_nosub(ft->str, first_line, line_len))
				return ft->name;
			break;
		}
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
