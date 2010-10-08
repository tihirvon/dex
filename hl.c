#include "window.h"
#include "state.h"

static int bitmap_get(const unsigned char *bitmap, unsigned int idx)
{
	unsigned int byte = idx / 8;
	unsigned int bit = idx & 7;
	return bitmap[byte] & 1 << bit;
}

static int is_buffered(const struct condition *cond, const char *str, int len)
{
	if (len != cond->u.cond_bufis.len)
		return 0;

	if (cond->u.cond_bufis.icase)
		return !strncasecmp(cond->u.cond_bufis.str, str, len);
	return !memcmp(cond->u.cond_bufis.str, str, len);
}

static int list_search(const char *str, int len, char **strings)
{
	int i;

	for (i = 0; strings[i]; i++) {
		const char *s = strings[i];
		if (str[0] == s[0] && !strncmp(str + 1, s + 1, len - 1)) {
			if (s[len] == 0)
				return 1;
		}
	}
	return 0;
}

static int in_list(struct string_list *list, const char *str, int len)
{
	char **strings = list->strings;
	int i;

	if (list->icase) {
		for (i = 0; strings[i]; i++) {
			if (!strncasecmp(str, strings[i], len) && strings[i][len] == 0)
				return 1;
		}
	} else {
		return list_search(str, len, strings);
	}
	return 0;
}

static int is_str(const struct condition *cond, const char *str)
{
	int len = cond->u.cond_str.len;
	if (cond->u.cond_str.icase)
		return !strncasecmp(cond->u.cond_str.str, str, len);
	return !memcmp(cond->u.cond_str.str, str, len);
}

// line should be terminated with \n unless it's the last line
static struct hl_color **highlight_line(struct state *state, const char *line, int len, struct state **ret)
{
	static struct hl_color **colors;
	static int alloc;
	int i = 0, sidx = -1;

	if (len > alloc) {
		alloc = ROUND_UP(len, 128);
		xrenew(colors, alloc);
	}

	while (1) {
		const struct condition *cond;
		unsigned char ch;
		int ci;
	top:
		if (i == len)
			break;
		ch = line[i];
		for (ci = 0; ci < state->nr_conditions; ci++) {
			int end;

			cond = &state->conditions[ci];
			switch (cond->type) {
			case COND_CHAR_BUFFER:
				if (!bitmap_get(cond->u.cond_char.bitmap, ch))
					break;
				// fall throught
			case COND_BUFFER:
				if (sidx < 0)
					sidx = i;
				colors[i++] = cond->emit_color;
				state = cond->destination.state;
				goto top;
			case COND_BUFIS:
				if (sidx >= 0 && is_buffered(cond, line + sidx, i - sidx)) {
					int idx;
					for (idx = sidx; idx < i; idx++)
						colors[idx] = cond->emit_color;
					sidx = -1;
					state = cond->destination.state;
					goto top;
				}
				break;
			case COND_CHAR:
				if (!bitmap_get(cond->u.cond_char.bitmap, ch))
					break;
				// fall through
			case COND_EAT:
				colors[i++] = cond->emit_color;
				// fall through
			case COND_NOEAT:
				sidx = -1;
				state = cond->destination.state;
				goto top;
			case COND_LISTED:
				if (sidx >= 0 && in_list(cond->u.cond_listed.list, line + sidx, i - sidx)) {
					int idx;
					for (idx = sidx; idx < i; idx++)
						colors[idx] = cond->emit_color;
					sidx = -1;
					state = cond->destination.state;
					goto top;
				}
				break;
			case COND_STR:
				end = i + cond->u.cond_str.len;
				if (len >= end && is_str(cond, line + i)) {
					while (i < end)
						colors[i++] = cond->emit_color;
					sidx = -1;
					state = cond->destination.state;
					goto top;
				}
				break;
			}
		}
	}

	if (ret)
		*ret = state;
	return colors;
}

static void resize_line_states(struct ptr_array *s, unsigned int count)
{
	if (s->alloc < count) {
		s->alloc = ROUND_UP(count, 64);
		xrenew(s->ptrs, s->alloc);
	}
}

static void move_line_states(struct ptr_array *s, int to, int from, int count)
{
	memmove(s->ptrs + to, s->ptrs + from, count * sizeof(*s->ptrs));
}

// lo should always be > 0 because start state of line 0 is constant.
// If bi points to line 0 then lo should be 1.
static void fill_start_states(struct block_iter *bi, int lo, int hi)
{
	struct ptr_array *s = &buffer->line_start_states;
	struct state *state = s->ptrs[lo - 1];
	int i;

	BUG_ON(lo < 1);
	for (i = lo; i <= hi; i++) {
		struct lineref lr;

		fill_line_nl_ref(bi, &lr);
		highlight_line(state, lr.line, lr.size, &state);
		block_iter_next_line(bi);
		s->ptrs[i] = state;
	}
}

void hl_fill_start_states(int line_nr)
{
	struct ptr_array *s = &buffer->line_start_states;
	struct block_iter bi = view->cursor;
	int lo, hi;

	if (!buffer->syn)
		return;

	if (line_nr < s->count) {
		// already filled
		return;
	}

	// fill from s->count to line_nr
	lo = s->count;
	hi = line_nr;

	block_iter_goto_line(&bi, lo - 1);
	resize_line_states(s, hi + 1);
	s->count = hi + 1;
	fill_start_states(&bi, lo, hi);
}

struct hl_color **hl_line(const char *line, int len, int line_nr)
{
	struct ptr_array *s = &buffer->line_start_states;
	struct hl_color **colors;
	struct state *next;

	if (!buffer->syn)
		return NULL;

	BUG_ON(line_nr >= s->count);
	colors = highlight_line(s->ptrs[line_nr], line, len, &next);
	if (line_nr + 1 == s->count) {
		resize_line_states(s, s->count + 1);
		s->ptrs[s->count++] = next;
	} else {
		BUG_ON(s->ptrs[line_nr + 1] != next);
	}
	return colors;
}

// called after text have been inserted to rehighlight changed lines
void hl_insert(int lines)
{
	struct ptr_array *s = &buffer->line_start_states;
	struct state *saved_state;
	struct block_iter bi;
	int first, last;

	if (!buffer->syn)
		return;

	update_cursor_y();

	// modified lines
	first = view->cy;
	last = first + lines;

	if (first >= s->count) {
		// nothing to rehighlight
		return;
	}

	if (last + 1 >= s->count) {
		// last already highlighted lines changed
		// there's nothing to gain, throw them away
		s->count = first + 1;
		return;
	}

	// add room for new line states
	if (lines) {
		int to = last + 1;
		int from = first + 1;
		resize_line_states(s, s->count + lines);
		move_line_states(s, to, from, s->count - from);
		s->count += lines;
	}

	// highlight modified and new lines
	bi = view->cursor;
	block_iter_bol(&bi);

	saved_state = s->ptrs[last + 1];
	fill_start_states(&bi, first + 1, last + 1);
	if (saved_state == s->ptrs[last + 1]) {
		// state of first unmodified line did not change
		return;
	}

	// rest of the lines are rehighlighted on demand
	s->count = last + 2;
	update_flags |= UPDATE_FULL;
}

// called after text have been deleted to rehighlight changed lines
void hl_delete(int deleted_nl)
{
	struct ptr_array *s = &buffer->line_start_states;
	struct state *saved_state;
	struct block_iter bi;
	int first, last, changed;

	if (!buffer->syn)
		return;

	if (s->count == 1)
		return;

	update_cursor_y();

	// modified lines
	first = view->cy;
	last = first + deleted_nl;

	if (first >= s->count) {
		// nothing to highlight
		return;
	}

	if (last + 1 >= s->count) {
		// last already highlighted lines changed
		// there's nothing to gain, throw them away
		s->count -= deleted_nl;
		return;
	}

	// there are already highlighted lines after changed lines
	// try to save the work.

	// remove deleted lines (states)
	if (deleted_nl) {
		int to = first + 1;
		int from = last + 1;
		move_line_states(s, to, from, s->count - from);
		s->count -= deleted_nl;
	}

	// with some luck we only need to highlight the only changed line
	changed = first;
	first = -1;
	last = -1;

	bi = view->cursor;
	block_iter_bol(&bi);
	saved_state = s->ptrs[changed + 1];
	fill_start_states(&bi, changed + 1, changed + 1);

	if (saved_state == s->ptrs[changed + 1]) {
		// start state of first unmodified line did not change
		return;
	}

	// rest of the lines are rehighlighted on demand
	s->count = changed + 2;

	update_flags |= UPDATE_FULL;
}
