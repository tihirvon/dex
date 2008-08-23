#include "filetype.h"
#include "common.h"

struct filetype {
	char *name;
	char *str;
	enum {
		FT_EXTENSION,
		FT_FILENAME,
		FT_CONTENT,
	} type;
};

static struct filetype **filetypes;
static int filetype_count;
static int filetype_alloc;

static void add_filetype(const char *name, const char *str, int type)
{
	struct filetype *ft;

	if (filetype_count == filetype_alloc) {
		filetype_alloc = filetype_alloc * 3 / 2;
		filetype_alloc = (filetype_alloc + 4) & ~3;
		xrenew(filetypes, filetype_alloc);
	}
	ft = xnew(struct filetype, 1);
	ft->name = xstrdup(name);
	ft->str = xstrdup(str);
	ft->type = type;
	filetypes[filetype_count++] = ft;
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

static int match(const char *pattern, const char *str)
{
	regex_t re;
	int rc;

	rc = regcomp(&re, pattern, REG_EXTENDED | REG_NEWLINE | REG_NOSUB);
	if (rc) {
		regfree(&re);
		return 0;
	}
	rc = regexec(&re, str, 0, NULL, 0);
	regfree(&re);
	return !rc;
}

const char *find_ft(const char *filename, const char *first_line)
{
	const char *ext = NULL;
	int i;

	if (filename)
		ext = strrchr(filename, '.');
	if (ext)
		ext++;
	for (i = 0; i < filetype_count; i++) {
		const struct filetype *ft = filetypes[i];
		switch (ft->type) {
		case FT_EXTENSION:
			if (ext && !strcmp(ext, ft->str))
				return ft->name;
			break;
		case FT_FILENAME:
			if (filename && match(ft->str, filename))
				return ft->name;
			break;
		case FT_CONTENT:
			if (first_line && match(ft->str, first_line))
				return ft->name;
			break;
		}
	}
	return NULL;
}

int is_ft(const char *name)
{
	int i;

	for (i = 0; i < filetype_count; i++) {
		if (!strcmp(filetypes[i]->name, name))
			return 1;
	}
	return 0;
}
