#include "search.h"
#include "buffer.h"
#include "editor.h"
#include "change.h"
#include "gbuf.h"
#include "edit.h"
#include "move.h"
#include "regexp.h"

#define MAX_SUBSTRINGS 32

static int do_search_fwd(regex_t *regex, int skip_first_char)
{
	struct block_iter bi = view->cursor;
	uchar u;

	if (skip_first_char)
		buffer_next_char(&bi, &u);
	do {
		regmatch_t match;
		struct lineref lr;

		if (block_iter_eof(&bi))
			return 0;

		fill_line_ref(&bi, &lr);
		if (!buf_regexec(regex, lr.line, lr.size, 1, &match, 0)) {
			block_iter_skip_bytes(&bi, match.rm_so);
			view->cursor = bi;
			view->center_on_scroll = 1;
			update_preferred_x();
			return 1;
		}
	} while (block_iter_next_line(&bi));
	return 0;
}

static int do_search_bwd(regex_t *regex)
{
	struct block_iter bi = view->cursor;
	int cx = view->cx_char;
	uchar u;

	block_iter_bol(&bi);
	if (block_iter_eof(&bi))
		goto next;

	do {
		regmatch_t match;
		struct lineref lr;
		int offset = -1;
		int pos = 0;

		fill_line_ref(&bi, &lr);
		while (!buf_regexec(regex, lr.line + pos, lr.size - pos, 1, &match, 0)) {
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
			block_iter_skip_bytes(&bi, offset);
			view->cursor = bi;
			view->center_on_scroll = 1;
			update_preferred_x();
			return 1;
		}
next:
		cx = -1;
	} while (block_iter_prev_line(&bi));
	return 0;
}

void search_tag(const char *pattern)
{
	regex_t regex;

	// NOTE: regex needs to be freed even if regcomp() fails
	if (regexp_compile(&regex, pattern, REG_NEWLINE)) {
		struct block_iter save = view->cursor;

		move_bof();
		if (do_search_fwd(&regex, 0)) {
			view->center_on_scroll = 1;
		} else {
			error_msg("Tag not found.");
			view->cursor = save;

			/* don't center view to cursor unnecessarily */
			view->force_center = 0;
		}
	}
	regfree(&regex);
}

static struct {
	regex_t regex;
	char *pattern;
	enum search_direction direction;

	/* if zero then regex hasn't been compiled */
	int re_flags;
} current_search;

void search_init(enum search_direction dir)
{
	current_search.direction = dir;
}

enum search_direction current_search_direction(void)
{
	return current_search.direction;
}

static void free_regex(void)
{
	if (current_search.re_flags) {
		regfree(&current_search.regex);
		current_search.re_flags = 0;
	}
}

static int update_regex(void)
{
	int re_flags = REG_EXTENDED | REG_NEWLINE;

	if (options.ignore_case)
		re_flags |= REG_ICASE;

	if (re_flags == current_search.re_flags)
		return 1;

	free_regex();

	current_search.re_flags = re_flags;
	if (regexp_compile(&current_search.regex, current_search.pattern, current_search.re_flags))
		return 1;

	free_regex();
	return 0;
}

void search(const char *pattern)
{
	free_regex();
	free(current_search.pattern);
	current_search.pattern = xstrdup(pattern);
	search_next();
}

void search_next(void)
{
	struct block_iter save;

	if (!current_search.pattern) {
		error_msg("No previous search pattern");
		return;
	}
	if (!update_regex())
		return;
	if (current_search.direction == SEARCH_FWD) {
		if (do_search_fwd(&current_search.regex, 1))
			return;
		save = view->cursor;
		move_bof();
		if (do_search_fwd(&current_search.regex, 0)) {
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
static int replace_on_line(struct lineref *lr, regex_t *re, const char *format,
	struct block_iter *bi, unsigned int *flagsp)
{
	char *buf = (char *)lr->line;
	unsigned int flags = *flagsp;
	regmatch_t m[MAX_SUBSTRINGS];
	size_t pos = 0;
	int eflags = 0;
	int nr = 0;

	while (!buf_regexec(re, buf + pos, lr->size - pos, MAX_SUBSTRINGS, m, eflags)) {
		int match_len = m[0].rm_eo - m[0].rm_so;
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
				goto out;
			}
		}

		if (skip) {
			/* move cursor after the matched text */
			block_iter_skip_bytes(&view->cursor, match_len);
		} else {
			char *str = build_replace(buf + pos, format, m);
			int nr_insert = strlen(str);

			/* lineref is invalidated by modification */
			if (buf == lr->line)
				buf = xmemdup(buf, lr->size);

			replace(match_len, str, nr_insert);
			free(str);
			nr++;

			/* update selection length */
			if (selecting()) {
				view->sel_eo += nr_insert;
				view->sel_eo -= match_len;
			}

			/* move cursor after the replaced text */
			block_iter_skip_bytes(&view->cursor, nr_insert);
		}
		*bi = view->cursor;

		if (!match_len)
			break;

		if (!(flags & REPLACE_GLOBAL))
			break;

		pos += m[0].rm_so + match_len;

		/* don't match beginning of line again */
		eflags = REG_NOTBOL;
	}
out:
	if (buf != lr->line)
		free(buf);
	return nr;
}

static unsigned int get_range(struct block_iter *bi)
{
	struct block_iter eof;

	if (selecting()) {
		struct selection_info info;
		init_selection(&info);
		view->cursor = info.si;
		view->sel_so = info.so;
		view->sel_eo = info.eo;
		*bi = view->cursor;
		return info.eo - info.so;
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

	if (flags & REPLACE_IGNORE_CASE)
		re_flags |= REG_ICASE;
	if (flags & REPLACE_BASIC)
		re_flags &= ~REG_EXTENDED;

	if (!regexp_compile(&re, pattern, re_flags)) {
		regfree(&re);
		return;
	}

	nr_bytes = get_range(&bi);

	/* record multiple changes as one chain only when replacing all */
	if (!(flags & REPLACE_CONFIRM))
		begin_change_chain();

	while (1) {
		// number of bytes to process
		unsigned int count;
		struct lineref lr;
		int nr;

		fill_line_ref(&bi, &lr);
		count = lr.size;
		if (lr.size > nr_bytes) {
			// end of selection is not full line
			lr.size = nr_bytes;
		}

		nr = replace_on_line(&lr, &re, format, &bi, &flags);
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
		info_msg("%d substitutions on %d lines", nr_substitutions, nr_lines);
	} else if (!(flags & REPLACE_CANCEL)) {
		info_msg("Pattern '%s' not found.", pattern);
	}
	select_end();
}
