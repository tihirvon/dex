#include "buffer-highlight.h"
#include "highlight.h"
#include "buffer.h"
#include "regexp.h"

/*
 * Contains one line including LF.
 * Used by syntax highlighter only.
 */
const char *hl_buffer;
size_t hl_buffer_len;

/*
 * Only available for highlighter and screen updates.
 * Never use while editing the buffer.  Use fetch_eol() when doing changes.
 */
void fetch_line(struct block_iter *bi)
{
	struct lineref lr;

	fill_line_nl_ref(bi, &lr);
	hl_buffer = lr.line;
	hl_buffer_len = lr.size;
	block_iter_next_line(bi);
}

static void init_highlighter(struct highlighter *h, struct buffer *b)
{
	memset(h, 0, sizeof(*h));
	h->headp = &b->hl_head;
	h->syn = b->syn;
}

static void init_highlighter_heredoc(struct highlighter *h)
{
	const struct syntax_context *c;
	struct block_iter bi;
	unsigned int offset;
	int eflags = 0;
	regmatch_t m[2];

	if (!h->stack.level) {
		BUG_ON(h->stack.heredoc_offset >= 0);
		return;
	}
	c = h->stack.contexts[h->stack.level];
	if (!(c->any.flags & SYNTAX_FLAG_HEREDOC)) {
		BUG_ON(h->stack.heredoc_offset >= 0);
		return;
	}

	BUG_ON(h->stack.heredoc_offset < 0);

	bi.head = &buffer->blocks;
	bi.blk = BLOCK(buffer->blocks.next);
	bi.offset = 0;
	block_iter_goto_offset(&bi, h->stack.heredoc_offset);
	offset = block_iter_bol(&bi);
	fetch_line(&bi);

	if (offset > 0)
		eflags |= REG_NOTBOL;
	if (buf_regexec(&c->sregex, hl_buffer + offset, hl_buffer_len - offset, 2, m, eflags)) {
		return;
	}
	if (m[1].rm_so >= 0) {
		const char *str = hl_buffer + m[1].rm_so + offset;
		int str_len = m[1].rm_eo - m[1].rm_so;

		build_heredoc_eregex(h, c, str, str_len);
	}
}

static int verify_count;
static int verify_counter;

static void verify_hl_list(struct list_head *head, const char *suffix)
{
#if DEBUG_SYNTAX
	struct hl_list *list;
#if DEBUG_SYNTAX > 1
	char buf[128];
	FILE *f;

	snprintf(buf, sizeof(buf), "/tmp/verify-%d-%d-%s", verify_count, verify_counter++, suffix);
	f = fopen(buf, "w");
	list_for_each_entry(list, head, node) {
		int i;

		for (i = 0; i < list->count; i++) {
			static const char *names[] = { " ", "{", "}" };
			struct hl_entry *e = &list->entries[i];
			union syntax_node *n = idx_to_syntax_node(hl_entry_idx(e));
			fprintf(f, "%3d %s %s\n", hl_entry_len(e), names[hl_entry_type(e) >> 6], n->any.name);
		}
	}
	fclose(f);
#endif

	list_for_each_entry(list, head, node)
		BUG_ON(!list->count);
#endif
}

static void full_debug(void)
{
#if DEBUG_SYNTAX > 1
	struct hl_list *list;
	unsigned int pos = 0;
	static int counter;
	char buf[128];
	FILE *f;
	int i;
	struct block_iter save = view->cursor;

	view->cursor.blk = BLOCK(buffer->blocks.next);
	view->cursor.offset = 0;

	snprintf(buf, sizeof(buf), "/tmp/hl-%d", counter++);
	f = fopen(buf, "w");
	list_for_each_entry(list, &buffer->hl_head, node) {
		BUG_ON(!list->count);
		for (i = 0; i < list->count; i++) {
			struct hl_entry *e = &list->entries[i];
			unsigned int len = hl_entry_len(e);
			char *bytes = buffer_get_bytes(len);
			union syntax_node *n = idx_to_syntax_node(hl_entry_idx(e));
			xrenew(bytes, len + 1);
			bytes[len] = 0;
			switch (hl_entry_type(e)) {
			case HL_ENTRY_NORMAL:
				fprintf(f, "[%s]%s", n->any.name, bytes);
				break;
			case HL_ENTRY_SOC:
				fprintf(f, "<%s>%s", n->any.name, bytes);
				break;
			case HL_ENTRY_EOC:
				fprintf(f, "</%s>%s", n->any.name, bytes);
				break;
			}
			free(bytes);
			pos += hl_entry_len(e);
			move_offset(pos);
		}
	}
	fclose(f);
	view->cursor = save;
#endif
}

static void verify_hl_size(void)
{
#if DEBUG_SYNTAX
	struct hl_list *list;
	struct block *blk;
	unsigned int hl_size = 0;
	unsigned int size = 0;
	int i;

	list_for_each_entry(list, &buffer->hl_head, node) {
		BUG_ON(!list->count);
		for (i = 0; i < list->count; i++) {
			struct hl_entry *e = &list->entries[i];
			hl_size += hl_entry_len(e);
		}
	}

	list_for_each_entry(blk, &buffer->blocks, node)
		size += blk->size;

	if (hl_size != size)
		BUG("hl_size != size, %d %d\n", hl_size, size);
#endif
}

void highlight_buffer(struct buffer *b)
{
	struct block_iter bi;
	struct highlighter h;

	if (!b->syn)
		return;

	bi.head = &b->blocks;
	bi.blk = BLOCK(b->blocks.next);
	bi.offset = 0;

	init_highlighter(&h, b);
	init_syntax_context_stack(&h.stack, b->syn->root);
	while (!block_iter_eof(&bi)) {
		fetch_line(&bi);
		h.line = hl_buffer;
		h.line_len = hl_buffer_len;
		h.offset = 0;
		highlight_line(&h);
	}
	free(h.words);
	free(h.matches);
	free(h.stack.contexts);
}

/*
 * stop_a and stop_b must be offset at beginning of a line. really?
 *
 * positions stop_a/b are included in the context stack
 */
static void update_contexts(const struct syntax *syn, struct list_head *head,
		unsigned int stop_a, struct syntax_context_stack *a,
		unsigned int stop_b, struct syntax_context_stack *b)
{
	unsigned int pos = 0;
	unsigned int stop = stop_a;
	struct syntax_context_stack *ptr = a;
	struct hl_list *list;

	init_syntax_context_stack(ptr, syn->root);
	list_for_each_entry(list, head, node) {
		int i;
		for (i = 0; i < list->count; i++) {
			struct hl_entry *e = &list->entries[i];
			unsigned int new_pos = pos + hl_entry_len(e);
			unsigned int type = hl_entry_type(e);

			if (type == HL_ENTRY_EOC) {
				const struct syntax_context *c = ptr->contexts[ptr->level];
				if (c->any.flags & SYNTAX_FLAG_HEREDOC)
					ptr->heredoc_offset = -1;
				pop_syntax_context(ptr);
				ds_print("%3d back %s\n", pos, ptr->contexts[ptr->level]->any.name);
			}
			if (type == HL_ENTRY_SOC) {
				const struct syntax_context *c;
				c = &idx_to_syntax_node(hl_entry_idx(e))->context;
				if (c->any.flags & SYNTAX_FLAG_HEREDOC)
					ptr->heredoc_offset = pos;
				push_syntax_context(ptr, c);
				ds_print("%3d new %s\n", pos, c->any.name);
			}
			while (new_pos > stop) {
				if (!b)
					return;

				copy_syntax_context_stack(b, a);
				stop = stop_b;
				ptr = b;
				b = NULL;
			}
			pos = new_pos;
		}
	}
	BUG("unreachable\n");
}

static void update_hl_eof(void)
{
	struct block_iter bi = view->cursor;
	struct syntax_context_stack a;
	struct highlighter h;
	unsigned int offset;

	block_iter_bol(&bi);
	offset = block_iter_get_offset(&bi);
	if (offset) {
		offset--;
		update_contexts(buffer->syn, &buffer->hl_head, offset, &a, 0, NULL);
		truncate_hl_list(&buffer->hl_head, offset + 1);
	} else {
		init_syntax_context_stack(&a, buffer->syn->root);
		truncate_hl_list(&buffer->hl_head, offset);
	}
	verify_hl_list(&buffer->hl_head, "truncate");

	init_highlighter(&h, buffer);
	copy_syntax_context_stack(&h.stack, &a);
	init_highlighter_heredoc(&h);

	/* highlight to eof */
	while (!block_iter_eof(&bi)) {
		fetch_line(&bi);
		h.line = hl_buffer;
		h.line_len = hl_buffer_len;
		h.offset = 0;
		highlight_line(&h);
	}
	if (h.heredoc_context)
		regfree(&h.heredoc_eregex);
	free(h.words);
	free(h.matches);
	free(h.stack.contexts);
	free(a.contexts);

	verify_hl_list(&buffer->hl_head, "final");
}

static int hl_context_stacks_equal(
	const struct syntax_context_stack *a,
	const struct syntax_context_stack *b)
{
	if (a->level != b->level)
		return 0;
	if (a->heredoc_offset >= 0 || b->heredoc_offset >= 0)
		return 0;
	return !memcmp(a->contexts, b->contexts, sizeof(a->contexts[0]) * (a->level + 1));
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
	LIST_HEAD(new_list);
	unsigned int to_prev, to_eol;
	unsigned int offset_a;
	unsigned int offset_b;
	struct syntax_context_stack a; /* context stack before first modified line */
	struct syntax_context_stack b; /* context stack after last modified line */
	struct block_iter bi;
	struct highlighter h;
	int i, top;

	if (!buffer->syn)
		return;

	verify_hl_list(&buffer->hl_head, "unmodified");

	bi = view->cursor;
	to_eol = 0;
	for (i = 0; i < ins_nl; i++)
		to_eol += block_iter_next_line(&bi);
	to_eol += block_iter_eol(&bi);

	if (block_iter_eof(&bi)) {
		/* last line was modified */
		update_hl_eof();
		verify_hl_size();
		return;
	}

	bi = view->cursor;
	to_prev = block_iter_bol(&bi);
	top = 1;
	offset_a = block_iter_get_offset(&bi);
	if (offset_a) {
		to_prev++;
		top = 0;
		offset_a--;
	}
	offset_b = offset_a + to_prev - ins_count + to_eol;

	if (top) {
		update_contexts(buffer->syn, &buffer->hl_head, offset_b, &b, 0, NULL);
		init_syntax_context_stack(&a, b.contexts[0]);
	} else {
		update_contexts(buffer->syn, &buffer->hl_head, offset_a, &a, offset_b, &b);
	}

	init_highlighter(&h, buffer);
	h.headp = &new_list;
	copy_syntax_context_stack(&h.stack, &a);
	init_highlighter_heredoc(&h);

	/* highlight the modified lines */
	for (i = 0; i <= ins_nl; i++) {
		fetch_line(&bi);
		h.line = hl_buffer;
		h.line_len = hl_buffer_len;
		h.offset = 0;
		highlight_line(&h);
	}

	ds_print("a=%d b=%d\n", offset_a, offset_b);

	for (i = 0; i <= a.level; i++)
		ds_print("a context[%d] = %s\n", i, a.contexts[i]->any.name);

	for (i = 0; i <= b.level; i++)
		ds_print("b context[%d] = %s\n", i, b.contexts[i]->any.name);

	for (i = 0; i <= h.stack.level; i++)
		ds_print("h context[%d] = %s\n", i, h.stack.contexts[i]->any.name);

	if (!hl_context_stacks_equal(&h.stack, &b)) {
		/* Syntax context changed.  We need to highlight to EOF. */
		struct hl_entry *e;

		truncate_hl_list(&buffer->hl_head, offset_a + 1 - top);
		verify_hl_list(&buffer->hl_head, "truncate");

		verify_hl_list(h.headp, "newline");

		/* add the highlighed lines */
		FOR_EACH_HL_ENTRY(e, h.headp) {
			merge_highlight_entry(&buffer->hl_head, e);
		} END_FOR_EACH_HL_ENTRY(e);
		free_hl_list(h.headp);
		verify_hl_list(&buffer->hl_head, "add1");

		/* highlight to eof */
		h.headp = &buffer->hl_head;
		while (!block_iter_eof(&bi)) {
			fetch_line(&bi);
			h.line = hl_buffer;
			h.line_len = hl_buffer_len;
			h.offset = 0;
			highlight_line(&h);
		}

		update_flags |= UPDATE_FULL;
	} else {
		struct list_head tmp_head;
		struct hl_entry *e;

		split_hl_list(&buffer->hl_head, offset_a + 1 - top, &tmp_head);
		verify_hl_list(&buffer->hl_head, "split-left");
		verify_hl_list(&tmp_head, "split-right");

		delete_hl_range(&tmp_head, 0, offset_b - offset_a + top);
		verify_hl_list(&tmp_head, "delete");

		verify_hl_list(h.headp, "newline");

		/* add the highlighed lines */
		FOR_EACH_HL_ENTRY(e, h.headp) {
			merge_highlight_entry(&buffer->hl_head, e);
		} END_FOR_EACH_HL_ENTRY(e);
		free_hl_list(h.headp);

		/* add rest */
		join_hl_lists(&buffer->hl_head, &tmp_head);

		if (ins_nl)
			update_flags |= UPDATE_FULL;
		else
			update_flags |= UPDATE_CURSOR_LINE;
	}
	if (h.heredoc_context)
		regfree(&h.heredoc_eregex);
	free(h.words);
	free(h.matches);
	free(h.stack.contexts);
	free(a.contexts);
	free(b.contexts);
	verify_hl_list(&buffer->hl_head, "final");
	verify_hl_size();
	full_debug();
	verify_count++;
	verify_counter = 0;
}
