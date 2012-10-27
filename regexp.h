#ifndef REGEXP_H
#define REGEXP_H

#include "libc.h"
#include <regex.h>

extern char *regexp_matches[];

bool regexp_match_nosub(const char *pattern, const char *buf, unsigned int len);
int regexp_match(const char *pattern, const char *buf, unsigned int len);
void free_regexp_matches(void);

bool regexp_compile(regex_t *re, const char *pattern, int flags);
bool regexp_exec(const regex_t *re, const char *buf, long size, long nr_m, regmatch_t *m, int flags);

#endif
