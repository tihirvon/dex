#include "buffer.h"
#include "change.h"
#include "search.h"
#include "gbuf.h"
#include "window.h"
#include "edit.h"

#define MAX_SUBSTRINGS 32

static int do_search_fwd(regex_t *regex)
{
	struct block_iter bi = view->cursor;
	uchar u;

	block_iter_next_byte(&bi, &u);
	do {
		regmatch_t match;

		fetch_eol(&bi);
		if (!regexec(regex, line_buffer, 1, &match, 0)) {
			int offset = match.rm_so;

			while (offset--)
				block_iter_next_byte(&bi, &u);
			SET_CURSOR(bi);
			update_preferred_x();
			return 1;
		}
	} while (block_iter_next_line(&bi));
	return 0;
}

static int do_search_bwd(regex_t *regex)
{
	struct block_iter bi = view->cursor;
	int cx = view->cx_idx;
	uchar u;

	do {
		regmatch_t match;
		int offset = -1;
		int pos = 0;

		block_iter_bol(&bi);
		fetch_eol(&bi);
		while (!regexec(regex, line_buffer + pos, 1, &match, 0)) {
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
			update_preferred_x();
			return 1;
		}
		cx = -1;
	} while (block_iter_prev_line(&bi));
	return 0;
}

void search_tag(const char *pattern)
{
	regex_t regex;
	int err;

	// NOTE: regex needs to be freed even if regcomp() fails
	err = regcomp(&regex, pattern, REG_NEWLINE);
	if (err) {
		char error[1024];
		regerror(err, &regex, error, sizeof(error));
		error_msg(error);
	} else {
		if (do_search_fwd(&regex))
			center_cursor();
	}
	regfree(&regex);
}

static struct {
	char error[1024];
	regex_t regex;
	char *pattern;
	enum search_direction direction;
} current_search;

void search_init(enum search_direction dir)
{
	current_search.direction = dir;
}

enum search_direction current_search_direction(void)
{
	return current_search.direction;
}

void search(const char *pattern, int re_flags)
{
	int err;

	if (current_search.pattern) {
		free(current_search.pattern);
		regfree(&current_search.regex);
		current_search.error[0] = 0;
	}
	current_search.pattern = xstrdup(pattern);

	// NOTE: regex needs to be freed even if regcomp() fails
	err = regcomp(&current_search.regex, pattern, re_flags);
	if (err) {
		regerror(err, &current_search.regex, current_search.error, sizeof(current_search.error));
		error_msg(current_search.error);
		return;
	}

	search_next();
}

void search_next(void)
{
	struct block_iter save;

	if (!current_search.pattern) {
		error_msg("No previous search pattern");
		return;
	}
	if (*current_search.error) {
		error_msg(current_search.error);
		return;
	}
	if (current_search.direction == SEARCH_FWD) {
		if (do_search_fwd(&current_search.regex))
			return;
		save = view->cursor;
		move_bof();
		if (do_search_fwd(&current_search.regex)) {
			info_msg("Continuing at top.");
		} else {
			info_msg("No matches.");
			view->cursor = save;
		}
	} else {
		if (do_search_bwd(&current_search.regex))
			return;
		save = view->cursor;
		move_eof();
		if (do_search_bwd(&current_search.regex)) {
			info_msg("Continuing at bottom.");
		} else {
			info_msg("No matches.");
			view->cursor = save;
		}
	}
}

void search_prev(void)
{
	current_search.direction ^= 1;
	search_next();
	current_search.direction ^= 1;
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
static int replace_on_line(regex_t *re, const char *format, struct block_iter *bi, unsigned int *flagsp)
{
	unsigned int flags = *flagsp;
	regmatch_t m[MAX_SUBSTRINGS];
	int nr = 0;

	while (!regexec(re, line_buffer, MAX_SUBSTRINGS, m, 0)) {
		int match_len, count;
		int skip = 0;

		/* move cursor to beginning of the text to replace */
		block_iter_skip_bytes(bi, m[0].rm_so);
		view->cursor = *bi;

		if (flags & REPLACE_CONFIRM) {
			switch (get_confirmation("Ynaq", "Replace?")) {
			case 'y':
				break;
			case 'n':
				skip = 1;
				break;
			case 'a':
				flags &= ~REPLACE_CONFIRM;
				*flagsp = flags;

				/* record rest of the changes as one chain */
				begin_change_chain();
				break;
			case 'q':
			case 0:
				*flagsp = flags | REPLACE_CANCEL;
				return nr;
			}
		}

		match_len = m[0].rm_eo - m[0].rm_so;
		if (skip) {
			/* move cursor after the matched text */
			block_iter_skip_bytes(&view->cursor, match_len);
		} else {
			char *str = build_replace(line_buffer, format, m);
			int nr_insert = strlen(str);

			replace(match_len, str, nr_insert);
			free(str);
			nr++;

			/* move cursor after the replaced text */
			block_iter_skip_bytes(&view->cursor, nr_insert);
		}
		*bi = view->cursor;

		if (!(flags & REPLACE_GLOBAL))
			break;

		count = m[0].rm_so + match_len;
		memmove(line_buffer, line_buffer + count, line_buffer_len - count + 1);
		line_buffer_len -= count;
	}
	return nr;
}

static unsigned int get_range(struct block_iter *bi)
{
	struct block_iter eof;

	if (view->sel.blk) {
		unsigned int len = prepare_selection();
		*bi = view->cursor;
		return len;
	}

	bi->head = &buffer->blocks;
	bi->blk = BLOCK(buffer->blocks.next);
	bi->offset = 0;

	eof.head = &buffer->blocks;
	eof.blk = BLOCK(buffer->blocks.prev);
	eof.offset = eof.blk->size;
	return block_iter_get_offset(&eof);
}

void reg_replace(const char *pattern, const char *format, unsigned int flags)
{
	struct block_iter bi;
	unsigned int nr_bytes;
	int re_flags = REG_EXTENDED | REG_NEWLINE;
	int nr_substitutions = 0;
	int nr_lines = 0;
	regex_t re;
	int err;

	if (flags & REPLACE_IGNORE_CASE)
		re_flags |= REG_ICASE;
	if (flags & REPLACE_BASIC)
		re_flags &= ~REG_EXTENDED;

	err = regcomp(&re, pattern, re_flags);
	if (err) {
		char error[1024];
		regerror(err, &re, error, sizeof(error));
		regfree(&re);
		error_msg(error);
		return;
	}

	nr_bytes = get_range(&bi);

	/* record multiple changes as one chain only when replacing all */
	if (!(flags & REPLACE_CONFIRM))
		begin_change_chain();

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

		nr = replace_on_line(&re, format, &bi, &flags);
		if (nr) {
			nr_substitutions += nr;
			nr_lines++;
		}
		if (flags & REPLACE_CANCEL)
			break;
		if (count + 1 >= nr_bytes)
			break;
		nr_bytes -= count + 1;

		BUG_ON(!block_iter_next_line(&bi));
	}

	if (!(flags & REPLACE_CONFIRM))
		end_change_chain();

	regfree(&re);

	if (nr_substitutions) {
		update_flags |= UPDATE_FULL;
		select_end();
	}

	if (nr_substitutions)
		info_msg("%d substitutions on %d lines", nr_substitutions, nr_lines);
	else if (!(flags & REPLACE_CANCEL))
		info_msg("Pattern '%s' not found.", pattern);
}
