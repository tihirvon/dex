#ifndef FILETYPE_H
#define FILETYPE_H

void add_ft_extensions(const char *name, char * const *extensions);
void add_ft_match(const char *name, const char *pattern);
void add_ft_content(const char *name, const char *pattern);
const char *find_ft(const char *filename, const char *first_line);
int is_ft(const char *name);

#endif
