#ifndef PATH_H
#define PATH_H

char *path_absolute(const char *filename);
char *relative_filename(const char *f, const char *cwd);
char *short_filename_cwd(const char *absolute, const char *cwd);
char *short_filename(const char *absolute);
const char *filename_to_utf8(const char *filename);
const char *display_filename(const char *filename);

#endif
