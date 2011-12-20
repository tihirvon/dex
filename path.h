#ifndef PATH_H
#define PATH_H

char *path_absolute(const char *filename);
char *short_filename_cwd(const char *absolute, const char *cwd);
char *short_filename(const char *absolute);

#endif
