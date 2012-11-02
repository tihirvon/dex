#ifndef REGEXP_H
#define REGEXP_H

#include "libc.h"
#include "ptr-array.h"
#include <regex.h>

bool regexp_match_nosub(const char *pattern, const char *buf, unsigned int len);
bool regexp_match(const char *pattern, const char *buf, long size, struct ptr_array *m);

bool regexp_compile(regex_t *re, const char *pattern, int flags);
bool regexp_exec(const regex_t *re, const char *buf, long size, long nr_m, regmatch_t *m, int flags);
bool regexp_exec_sub(const regex_t *re, const char *buf, long size, struct ptr_array *matches, int flags);

#endif
