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

static char *detect(const char *pattern, const char *buf, unsigned int len, const char *name)
{
	if (!buf)
		return NULL;

	if (name[0] == '\\') {
		char *end, *ret;
		long n, idx = strtol(name + 1, &end, 10);

		if (name[1] && !*end) {
			n = regexp_match(pattern, buf, len);
			if (idx < 1 || (n && idx > n)) {
				free_regexp_matches();
				return NULL;
			}
			ret = regexp_matches[idx];
			regexp_matches[idx] = NULL;
			free_regexp_matches();
			return ret;
		}
	}

	if (!regexp_match_nosub(pattern, buf, len))
		return NULL;
	return xstrdup(name);
}

char *find_ft(const char *filename, const char *first_line, unsigned int line_len)
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
		char *name = NULL;

		switch (ft->type) {
		case FT_EXTENSION:
			if (ext && !strcmp(ext, ft->str))
				name = xstrdup(ft->name);
			break;
		case FT_FILENAME:
			name = detect(ft->str, filename, filename_len, ft->name);
			break;
		case FT_CONTENT:
			name = detect(ft->str, first_line, line_len, ft->name);
			break;
		}
		if (name)
			return name;
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
