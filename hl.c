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
	char **strings = list->u.strings;
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

static int in_hash(struct string_list *list, const char *str, int len)
{
	unsigned int hash = buf_hash(str, len);
	struct hash_str *h = list->u.hash[hash % ARRAY_COUNT(list->u.hash)];

	if (list->icase) {
		while (h) {
			if (len == h->len && !strncasecmp(str, h->str, len))
				return 1;
			h = h->next;
		}
	} else {
		while (h) {
			if (len == h->len && !memcmp(str, h->str, len))
				return 1;
			h = h->next;
		}
	}
	return 0;
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
			cond = &state->conditions[ci];
			switch (cond->type) {
			case COND_CHAR_BUFFER:
				if (!bitmap_get(cond->u.cond_char.bitmap, ch))
					break;
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
			case COND_LISTED_HASH:
				if (sidx >= 0 && in_hash(cond->u.cond_listed.list, line + sidx, i - sidx)) {
					int idx;
					for (idx = sidx; idx < i; idx++)
						colors[idx] = cond->emit_color;
					sidx = -1;
					state = cond->destination.state;
					goto top;
				}
				break;
			case COND_RECOLOR: {
				int idx = i - cond->u.cond_recolor.len;
				if (idx < 0)
					idx = 0;
				while (idx < i)
					colors[idx++] = cond->emit_color;
				} break;
			case COND_STR: {
				int slen = cond->u.cond_str.len;
				int end = i + slen;
				if (len >= end && !memcmp(cond->u.cond_str.str, line + i, slen)) {
					while (i < end)
						colors[i++] = cond->emit_color;
					sidx = -1;
					state = cond->destination.state;
					goto top;
				}
				} break;
			case COND_STR_ICASE: {
				int slen = cond->u.cond_str.len;
				int end = i + slen;
				if (len >= end && !strncasecmp(cond->u.cond_str.str, line + i, slen)) {
					while (i < end)
						colors[i++] = cond->emit_color;
					sidx = -1;
					state = cond->destination.state;
					goto top;
				}
				} break;
			case COND_STR2:
				// optimized COND_STR (length 2, case sensitive)
				if (ch == cond->u.cond_str.str[0] && len - i > 1 &&
						line[i + 1] == cond->u.cond_str.str[1]) {
					colors[i++] = cond->emit_color;
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

static void truncate_line_states(int count)
{
	struct ptr_array *s = &buffer->line_start_states;

	BUG_ON(buffer->first_hole > s->count);
	s->count = count;
	if (buffer->first_hole > s->count)
		buffer->first_hole = s->count;
}

static void new_hole(int idx)
{
	struct ptr_array *s = &buffer->line_start_states;

	if (idx >= buffer->first_hole)
		return;

	/*
	 * hl_line() can fill hole with states and leave buffer->first_hole
	 * point to non-NULL value.
	 */
	if (buffer->first_hole < s->count)
		s->ptrs[buffer->first_hole] = NULL;

	buffer->first_hole = idx;
}

static void find_hole(int pos)
{
	struct ptr_array *s = &buffer->line_start_states;
	while (pos < s->count && s->ptrs[pos])
		pos++;
	buffer->first_hole = pos;
}

static void block_iter_move_down(struct block_iter *bi, int count)
{
	while (count--)
		block_iter_next_line(bi);
}

void hl_fill_start_states(int line_nr)
{
	struct ptr_array *s = &buffer->line_start_states;
	struct block_iter bi;
	int current_line = 0;

	if (!buffer->syn)
		return;

	buffer_bof(&bi);
	resize_line_states(s, line_nr + 1);
	while (1) {
		struct lineref lr;
		struct state *st;
		int idx;

		// always true: buffer->first_hole <= s->count
		BUG_ON(buffer->first_hole > s->count);
		if (buffer->first_hole > line_nr)
			break;

		// go to line before first hole
		block_iter_move_down(&bi, buffer->first_hole - 1 - current_line);
		current_line = buffer->first_hole - 1;
		idx = current_line;

		fill_line_nl_ref(&bi, &lr);
		highlight_line(s->ptrs[idx++], lr.line, lr.size, &st);

		BUG_ON(idx > s->count);
		if (idx == s->count) {
			// new
			s->ptrs[idx] = st;
			s->count++;
			buffer->first_hole = s->count;
		} else if (!s->ptrs[idx]) {
			s->ptrs[idx] = st;
			buffer->first_hole++;
		} else {
			if (s->ptrs[idx] == st) {
				// hole successfully closed. find next
				find_hole(idx + 1);
			} else {
				// hole filled but state changed
				s->ptrs[idx] = st;
				buffer->first_hole = idx + 1;
			}
		}
	}
}

struct hl_color **hl_line(const char *line, int len, int line_nr, int *next_changed)
{
	struct ptr_array *s = &buffer->line_start_states;
	struct hl_color **colors;
	struct state *next;

	*next_changed = 0;
	if (!buffer->syn)
		return NULL;

	BUG_ON(line_nr >= s->count);
	colors = highlight_line(s->ptrs[line_nr++], line, len, &next);

	if (line_nr == s->count) {
		resize_line_states(s, s->count + 1);
		s->ptrs[s->count++] = next;
		buffer->first_hole = s->count;
		*next_changed = 1;
	} else if (!s->ptrs[line_nr]) {
		s->ptrs[line_nr] = next;
		// NOTE: this can leave first_hole point to non-NULL state
		buffer->first_hole = line_nr + 1;
		*next_changed = 1;
	} else {
		if (line_nr == buffer->first_hole) {
			if (s->ptrs[line_nr] == next) {
				// hole successfully closed
				find_hole(line_nr + 1);
			} else {
				// hole filled but state changed
				s->ptrs[buffer->first_hole++] = next;
				*next_changed = 1;
			}
		} else {
			BUG_ON(s->ptrs[line_nr] != next);
		}
	}
	return colors;
}

// called after text have been inserted to rehighlight changed lines
void hl_insert(int lines)
{
	struct ptr_array *s = &buffer->line_start_states;
	int first, last, i;

	update_cursor_y();

	// modified lines
	first = view->cy;
	last = first + lines;
	lines_changed(first, first == last ? first : INT_MAX);

	if (!buffer->syn)
		return;

	if (first >= s->count) {
		// nothing to rehighlight
		return;
	}

	if (last + 1 >= s->count) {
		// last already highlighted lines changed
		// there's nothing to gain, throw them away
		truncate_line_states(first + 1);
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

	// invalidate start states of lines right after any changed lines
	for (i = first + 1; i <= last + 1; i++)
		s->ptrs[i] = NULL;
	new_hole(first + 1);
}

// called after text have been deleted to rehighlight changed lines
void hl_delete(int deleted_nl)
{
	struct ptr_array *s = &buffer->line_start_states;
	int first, last;

	update_cursor_y();

	// modified lines
	first = view->cy;
	last = first + deleted_nl;
	lines_changed(first, first == last ? first : INT_MAX);

	if (!buffer->syn)
		return;

	if (s->count == 1)
		return;

	if (first >= s->count) {
		// nothing to highlight
		return;
	}

	if (last + 1 >= s->count) {
		// last already highlighted lines changed
		// there's nothing to gain, throw them away
		truncate_line_states(s->count - deleted_nl);
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

	// invalidate line state after the changed line
	s->ptrs[first + 1] = NULL;
	new_hole(first + 1);
}
