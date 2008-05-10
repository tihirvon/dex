#include "xmalloc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void malloc_fail(void)
{
	fprintf(stderr, "out of memory\n");
	exit(42);
}

char *xstrndup(const char *str, size_t n)
{
	int len;
	char *s;

	for (len = 0; len < n && str[len]; len++)
		;
	s = xmalloc(len + 1);
	memcpy(s, str, len);
	s[len] = 0;
	return s;
}
