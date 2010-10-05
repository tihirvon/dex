#include "hl.h"
#include "buffer.h"
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
struct hl_color **highlight_line(struct state *state, const char *line, int len, struct state **ret)
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

void highlight_buffer(struct buffer *b)
{
	struct block_iter bi;
	struct state *state;

	if (!b->syn)
		return;

	b->line_states.count = 0;
	if (b->line_states.alloc < buffer->nl) {
		b->line_states.alloc = ROUND_UP(buffer->nl, 16);
		xrenew(b->line_states.ptrs, b->line_states.alloc);
	}

	bi.head = &b->blocks;
	bi.blk = BLOCK(b->blocks.next);
	bi.offset = 0;

	state = b->syn->states.ptrs[0];
	while (!block_iter_is_eof(&bi)) {
		struct lineref lr;

		b->line_states.ptrs[b->line_states.count++] = state;

		fill_line_nl_ref(&bi, &lr);
		highlight_line(state, lr.line, lr.size, &state);

		block_iter_next_line(&bi);
	}
}

/*
 * NOTE: This is called after delete too.
 *
 * Delete:
 *     ins_nl is 0
 *     ins_count is negative
 */
void update_hl_insert(unsigned int ins_nl, int ins_count)
{
	if (!buffer->syn)
		return;

	// FIXME: don't waste CPU
	highlight_buffer(buffer);
}
