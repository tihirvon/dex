#ifndef ENV_H
#define ENV_H

#include "gbuf.h"
#include "libc.h"

void collect_builtin_env(const char *prefix, int len);
bool expand_builtin_env(struct gbuf *buf, const char *name, int len);

#endif
