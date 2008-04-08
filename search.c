#include "buffer.h"
#include "search.h"
#include "util.h"
#include "gbuf.h"

#include <regex.h>

#define MAX_SUBSTRINGS 32

enum {
	REPLACE_CONFIRM = (1 << 0),
	REPLACE_GLOBAL = (1 << 1),
};

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
	do {
		regmatch_t match;

		fetch_eol(&bi);
		if (!regexec(&regex, line_buffer, 1, &match, 0)) {
			int offset = match.rm_so;

			while (offset--)
				block_iter_next_byte(&bi, &u);
			SET_CURSOR(bi);
			update_cursor(view);
			return;
		}
	} while (block_iter_next_line(&bi));
}

static void do_search_bwd(void)
{
	struct block_iter bi = view->cursor;
	int cx = view->cx_idx;
	uchar u;

	do {
		regmatch_t match;
		const char *buf = line_buffer;
		int offset = -1;
		int pos = 0;

		block_iter_bol(&bi);
		fetch_eol(&bi);
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
		cx = -1;
	} while (block_iter_prev_line(&bi));
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
		d_print("error: %s\n", regex_error);
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

static char *build_replace(const char *line, const char *format, regmatch_t *m)
{
	GBUF(buf);
	int i = 0;

	while (format[i]) {
		int ch = format[i++];
		int n = 0;
		int count = 0;

		if (ch != '\\') {
			gbuf_add_ch(&buf, ch);
			continue;
		}
		while (isdigit(format[i])) {
			n *= 10;
			n += format[i++] - '0';
			count++;
		}
		if (!count) {
			gbuf_add_ch(&buf, format[i++]);
		} else if (n < MAX_SUBSTRINGS) {
			int len = m[n].rm_eo - m[n].rm_so;
			if (len > 0)
				gbuf_add_buf(&buf, line + m[n].rm_so, len);
		}
	}
	return gbuf_steal(&buf);
}

/*
 * s/abc/x
 *
 * string                to match against
 * -------------------------------------------
 * "foo abc bar abc baz" "foo abc bar abc baz"
 * "foo x bar abc baz"   " bar abc baz"
 */
static int replace_on_line(regex_t *re, const char *format, struct block_iter *bi, unsigned int flags)
{
	regmatch_t m[MAX_SUBSTRINGS];
	int nr = 0;

	while (!regexec(re, line_buffer, MAX_SUBSTRINGS, m, 0)) {
		int nr_delete, nr_insert, count;
		char *str;
		uchar u;

		str = build_replace(line_buffer, format, m);

		count = m[0].rm_so;
		while (count--)
			block_iter_next_byte(bi, &u);
		view->cursor = *bi;

		nr_delete = m[0].rm_eo - m[0].rm_so;
		nr_insert = strlen(str);
		replace(nr_delete, str, nr_insert);
		nr++;

		count = nr_insert;
		while (count--)
			block_iter_next_byte(&view->cursor, &u);
		*bi = view->cursor;

		if (!(flags & REPLACE_GLOBAL))
			break;

		count = m[0].rm_so + nr_delete;
		memmove(line_buffer, line_buffer + count, line_buffer_len - count + 1);
		line_buffer_len -= count;
	}
	return nr;
}

static void get_range(struct block_iter *bi, unsigned int *nr_bytes)
{
	struct block_iter sbi, ebi;
	unsigned int so, eo, len;

	if (!view->sel.blk) {
		struct block_iter eof;

		eof.head = &buffer->blocks;
		eof.blk = BLOCK(buffer->blocks.prev);
		eof.offset = eof.blk->size;
		*nr_bytes = block_iter_get_offset(&eof);

		bi->head = &buffer->blocks;
		bi->blk = BLOCK(buffer->blocks.next);
		bi->offset = 0;
		return;
	}

	sbi = view->sel;
	ebi = view->cursor;
	so = block_iter_get_offset(&sbi);
	eo = block_iter_get_offset(&ebi);
	if (so > eo) {
		struct block_iter tbi = sbi;
		unsigned int to = so;
		sbi = ebi;
		ebi = tbi;
		so = eo;
		eo = to;
	}
	len = eo - so;
	if (view->sel_is_lines) {
		len += block_iter_bol(&sbi);
		len += count_bytes_eol(&ebi);
	} else {
		len++;
	}

	*bi = sbi;
	*nr_bytes = len;
}

void reg_replace(const char *pattern, const char *format, const char *flags_str)
{
	struct block_iter bi;
	unsigned int nr_bytes;
	unsigned int flags = 0;
	int re_flags = REG_EXTENDED | REG_NEWLINE;
	int nr_substitutions = 0;
	int nr_lines = 0;
	regex_t re;
	int i, err;

	for (i = 0; flags_str && flags_str[i]; i++) {
		switch (flags_str[i]) {
		case 'c':
			flags |= REPLACE_CONFIRM;
			break;
		case 'g':
			flags |= REPLACE_GLOBAL;
			break;
		case 'i':
			re_flags |= REG_ICASE;
			break;
		default:
			break;
		}
	}

	err = regcomp(&re, pattern, re_flags);
	if (err) {
		regerror(err, &re, regex_error, sizeof(regex_error));
		regfree(&re);
		d_print("error: %s\n", regex_error);
		return;
	}

	get_range(&bi, &nr_bytes);
	while (1) {
		// number of bytes to process
		unsigned int count;
		int nr;

		fetch_eol(&bi);
		count = line_buffer_len;
		if (line_buffer_len > nr_bytes) {
			// end of selection is not full line
			line_buffer[nr_bytes] = 0;
		}

		nr = replace_on_line(&re, format, &bi, flags);
		if (nr) {
			nr_substitutions += nr;
			nr_lines++;
		}
		if (count >= nr_bytes)
			break;
		nr_bytes -= count + 1;

		BUG_ON(!block_iter_next_line(&bi));
	}
	update_cursor(view);
	regfree(&re);

	if (nr_substitutions)
		update_flags |= UPDATE_FULL;

	d_print("%d substitutions on %d lines\n", nr_substitutions, nr_lines);
}
