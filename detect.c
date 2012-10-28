#include "detect.h"
#include "buffer.h"
#include "regexp.h"

static bool next_line(struct block_iter *bi, struct lineref *lr)
{
	if (!block_iter_eat_line(bi))
		return false;
	fill_line_ref(bi, lr);
	return true;
}

/*
 * Parse #! line and return interpreter name without vesion number.
 * For example if file's first line is "#!/usr/bin/env python2" then
 * "python" is returned.
 */
char *detect_interpreter(struct buffer *b)
{
	BLOCK_ITER(bi, &b->blocks);
	struct lineref lr;
	char *ret;
	int n;

	fill_line_ref(&bi, &lr);
	n = regexp_match("^#!\\s*/.*(/env\\s+|/)([a-zA-Z_-]+)[0-9.]*(\\s|$)",
		lr.line, lr.size);
	if (!n)
		return NULL;

	ret = xstrdup(regexp_matches[2]);
	free_regexp_matches();

	if (strcmp(ret, "sh"))
		return ret;

	/*
	 * #!/bin/sh
	 * # the next line restarts using wish \
	 * exec wish "$0" "$@"
	 */
	if (!next_line(&bi, &lr) || !regexp_match_nosub("^#.*\\\\$", lr.line, lr.size))
		return ret;

	if (!next_line(&bi, &lr) || !regexp_match_nosub("^exec\\s+wish\\s+", lr.line, lr.size))
		return ret;

	free(ret);
	return xstrdup("wish");
}
