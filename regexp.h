#ifndef REGEXP_H
#define REGEXP_H

#include <regex.h>

extern char *regexp_matches[];

int regexp_match_nosub(const char *pattern, const char *buf, unsigned int len);
int regexp_match(const char *pattern, const char *buf, unsigned int len);
void free_regexp_matches(void);
int regexp_compile(regex_t *regexp, const char *pattern, int flags);
int buf_regexec(const regex_t *regexp, const char *buf,
	unsigned int size, size_t nr_m, regmatch_t *m, int flags);

#endif
