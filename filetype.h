#ifndef FILETYPE_H
#define FILETYPE_H

enum detect_type {
	FT_EXTENSION,
	FT_FILENAME,
	FT_CONTENT,
};

void add_filetype(const char *name, const char *str, enum detect_type type);
char *find_ft(const char *filename, const char *first_line, unsigned int line_len);
int is_ft(const char *name);

#endif
