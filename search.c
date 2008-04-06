#include "buffer.h"
#include "search.h"
#include "util.h"

#include <regex.h>

enum search_direction search_direction;

static char *search_pattern;
static regex_t regex;
static char regex_error[1024];

void search_init(enum search_direction dir)
{
	search_direction = dir;
}

static void do_search_fwd(void)
{
	struct block_iter bi = view->cursor;
	uchar u;

	block_iter_next_byte(&bi, &u);
	fetch_eol(&bi);
	while (1) {
		regmatch_t match;

		if (!regexec(&regex, line_buffer, 1, &match, 0)) {
			int offset = match.rm_so;

			while (offset--)
				block_iter_next_byte(&bi, &u);
			SET_CURSOR(bi);
			update_cursor(view);
			return;
		}
		if (!block_iter_next_line(&bi))
			break;
		fetch_eol(&bi);
	}
}

static void do_search_bwd(void)
{
	struct block_iter bi = view->cursor;
	int cx = view->cx_idx;
	uchar u;

	block_iter_bol(&bi);
	fetch_eol(&bi);
	while (1) {
		regmatch_t match;
		const char *buf = line_buffer;
		int offset = -1;
		int pos = 0;

		while (!regexec(&regex, buf + pos, 1, &match, 0)) {
			pos += match.rm_so;
			if (cx >= 0 && pos >= cx) {
				/* match at or after cursor */
				break;
			}

			/* this might be what we want (last match before cursor) */
			offset = pos;
			pos++;
		}

		if (offset >= 0) {
			while (offset--)
				block_iter_next_byte(&bi, &u);
			SET_CURSOR(bi);
			update_cursor(view);
			return;
		}
		if (!block_iter_prev_line(&bi))
			break;
		cx = -1;
		block_iter_bol(&bi);
		fetch_eol(&bi);
	}
}

void search(const char *pattern)
{
	int err;

	if (search_pattern) {
		free(search_pattern);
		regfree(&regex);
	}
	search_pattern = xstrdup(pattern);

	// NOTE: regex needs to be freed even if regcomp() fails
	err = regcomp(&regex, pattern, REG_EXTENDED | REG_NEWLINE);
	if (err) {
		regerror(err, &regex, regex_error, sizeof(regex_error));
		return;
	}

	search_next();
}

static int can_search(void)
{
	if (!search_pattern) {
		d_print("no previous search pattern\n");
		return 0;
	}
	if (*regex_error) {
		d_print("Error parsing regexp: %s\n", regex_error);
		return 0;
	}
	return 1;
}

void search_next(void)
{
	if (can_search()) {
		if (search_direction == SEARCH_FWD)
			do_search_fwd();
		else
			do_search_bwd();
	}
}

void search_prev(void)
{
	if (can_search()) {
		if (search_direction == SEARCH_BWD)
			do_search_fwd();
		else
			do_search_bwd();
	}
}
