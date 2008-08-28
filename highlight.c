#include "color.h"
#include "syntax.h"
#include "highlight.h"

static void update_join_color(struct syntax *syn, union syntax_node *node)
{
	int i, j;
	for (i = 0; i < syn->nr_join; i++) {
		const struct syntax_join *join = &syn->join[i];
		for (j = 0; j < join->nr_nodes; j++) {
			if (join->nodes[j] == node) {
				node->any.color = find_color(join->name);
				return;
			}
		}
	}
}

void update_syntax_colors(struct syntax *syntax)
{
	int i;

	for (i = 0; i < syntax->nr_nodes; i++) {
		union syntax_node *node = syntax->nodes[i];
		node->any.color = find_color(node->any.name);
		if (!node->any.color)
			update_join_color(syntax, node);
	}
}

void update_all_syntax_colors(void)
{
	struct syntax *s;

	list_for_each_entry(s, &syntaxes, node)
		update_syntax_colors(s);
}

void push_syntax_context(struct syntax_context_stack *stack, const struct syntax_context *c)
{
	if (++stack->level == stack->alloc) {
		stack->alloc = (stack->level + 8) & ~7;
		xrenew(stack->contexts, stack->alloc);
	}
	stack->contexts[stack->level] = c;
}

void copy_syntax_context_stack(struct syntax_context_stack *dst, const struct syntax_context_stack *src)
{
	dst->alloc = src->alloc;
	dst->level = src->level;
	if (src->alloc) {
		dst->contexts = xnew(const struct syntax_context *, dst->alloc);
		memcpy(dst->contexts, src->contexts, sizeof(src->contexts[0]) * (src->level + 1));
	} else {
		dst->contexts = NULL;
	}
}

static void add_hl_entry_at(struct hl_list *list, unsigned char desc, unsigned char len)
{
	struct hl_entry *e;

	if (list->count == ARRAY_COUNT(list->entries)) {
		struct hl_list *new = xnew(struct hl_list, 1);
		new->count = 0;
		list_add_after(&new->node, &list->node);
		list = new;
	}
	e = &list->entries[list->count++];
	e->desc = desc;
	e->len = len;
}

static void add_hl_entry(struct list_head *head, unsigned char desc, unsigned char len)
{
	BUG_ON(!len);
	if (list_empty(head)) {
		struct hl_list *list = xnew(struct hl_list, 1);
		struct hl_entry *e = &list->entries[0];
		e->desc = desc;
		e->len = len;
		list->count = 1;
		list_add_before(&list->node, head);
		return;
	}
	add_hl_entry_at(HL_LIST(head->prev), desc, len);
}

static void add_highlight(struct list_head *head, unsigned char desc, int len)
{
	int i;

	if (head->prev != head) {
		struct hl_list *last = HL_LIST(head->prev);
		struct hl_entry *entry = &last->entries[last->count - 1];
		if (entry->desc == desc && hl_entry_type(entry) == HL_ENTRY_NORMAL) {
			int sum = entry->len + len;
			if (sum <= 255) {
				entry->len = sum;
				return;
			}
			len -= 255 - entry->len;
			entry->len = 255;
		}
	}
	for (i = 0; i < len / 255; i++)
		add_hl_entry(head, desc, 255);
	if (len % 255)
		add_hl_entry(head, desc, len % 255);
}

void merge_highlight_entry(struct list_head *head, const struct hl_entry *e)
{
	add_highlight(head, e->desc, e->len);
}

static void add_node(struct highlighter *h, const union syntax_node *node, int len, unsigned int type)
{
	add_highlight(h->headp, get_syntax_node_idx(h->syn, node) | type, len);
}

static int hl_match_cmp(const void *ap, const void *bp)
{
	const struct hl_match *a = (const struct hl_match *)ap;
	const struct hl_match *b = (const struct hl_match *)bp;
	return a->match.rm_so - b->match.rm_so;
}

static void add_matches(struct highlighter *h, const union syntax_node *n, const regex_t *regex)
{
	int eoc = n->any.type == SYNTAX_NODE_CONTEXT && &n->context.eregex == regex;
	int offset = h->offset;
	int eflags = 0;
	regmatch_t m;

	if (offset > 0)
		eflags |= REG_NOTBOL;
	while (!regexec(regex, h->line + offset, 1, &m, eflags)) {
		int len = m.rm_eo - m.rm_so;

		m.rm_so += offset;
		m.rm_eo += offset;

		d_print("s=%d e=%d line_len=%d %s\n", m.rm_so, m.rm_eo, h->line_len, n->any.name);
		if (!len)
			break;

		if (h->nr_matches == h->alloc) {
			h->alloc += 16;
			xrenew(h->matches, h->alloc);
		}
		h->matches[h->nr_matches].node = n;
		h->matches[h->nr_matches].match = m;
		h->matches[h->nr_matches].eoc = eoc;
		h->nr_matches++;

		if (n->any.type == SYNTAX_NODE_CONTEXT) {
			/* need to restart */
			break;
		}

		offset = m.rm_eo;
		if (offset == h->line_len && !len)
			break;
	}
}

static int highlight_line_context(struct highlighter *h)
{
	const struct syntax_context *context = h->stack.contexts[h->stack.level];
	int eflags = 0;
	int i, offset;

	d_print("line: '%s'\n", h->line + h->offset);
	if (h->offset > 0)
		eflags |= REG_NOTBOL;

	h->nr_matches = 0;
	if (h->stack.level)
		add_matches(h, (const union syntax_node *)context, &context->eregex);
	for (i = 0; i < context->nr_nodes; i++) {
		const union syntax_node *n = context->nodes[i];

		switch (n->any.type) {
		case SYNTAX_NODE_WORD:
			add_matches(h, n, &n->word.regex);
			break;
		case SYNTAX_NODE_PATTERN:
			add_matches(h, n, &n->pattern.regex);
			break;
		case SYNTAX_NODE_CONTEXT:
			add_matches(h, n, &n->context.sregex);
			break;
		}
	}
	if (h->nr_matches)
		qsort(h->matches, h->nr_matches, sizeof(*h->matches), hl_match_cmp);

	offset = h->offset;
	for (i = 0; i < h->nr_matches; i++) {
		const struct hl_match *m = &h->matches[i];

		if (offset > m->match.rm_so) {
			/* ignore overlapping pattern */
			d_print("ignoring %s %d %d\n", m->node->any.name, offset, m->match.rm_so);
			continue;
		}
		d_print("M %-16s %d %d %d\n", m->node->any.name, offset, m->match.rm_so, m->match.rm_eo);

		if (offset < m->match.rm_so)
			add_node(h, (const union syntax_node *)context, m->match.rm_so - offset, HL_ENTRY_NORMAL);
		offset = m->match.rm_eo;

		if (m->node->any.type == SYNTAX_NODE_CONTEXT) {
			const struct syntax_context *c = &m->node->context;
			h->offset = offset;
			if (m->eoc) {
				/* current context ends */
				add_node(h, m->node, m->match.rm_eo - m->match.rm_so, HL_ENTRY_EOC);
				pop_syntax_context(&h->stack);
				d_print("back %s => %s\n", context->any.name, h->stack.contexts[h->stack.level]->any.name);
				return offset == h->line_len;
			} else {
				/* new context begins */
				add_node(h, m->node, m->match.rm_eo - m->match.rm_so, HL_ENTRY_SOC);
				push_syntax_context(&h->stack, c);
				d_print("new %s => %s\n", context->any.name, c->any.name);
				return offset == h->line_len;
			}
		}
		add_node(h, m->node, m->match.rm_eo - m->match.rm_so, HL_ENTRY_NORMAL);
	}

	if (offset < h->line_len)
		add_node(h, (const union syntax_node *)context, h->line_len - offset, HL_ENTRY_NORMAL);
	return 1;
}

void highlight_line(struct highlighter *h)
{
	while (!highlight_line_context(h))
		;
}

void free_hl_list(struct list_head *head)
{
	struct list_head *node = head->next;

	while (node != head) {
		struct list_head *next = node->next;
		free(HL_LIST(node));
		node = next;
	}
	list_init(head);
}

static void free_rest(struct list_head *head, struct list_head *first)
{
	struct list_head *last = first->prev;
	struct list_head *list = first;

	while (list != head) {
		struct list_head *next = list->next;
		free(HL_LIST(list));
		list = next;
	}
	last->next = head;
	head->prev = last;
}

void truncate_hl_list(struct list_head *head, unsigned int new_size)
{
	struct hl_list *list;
	unsigned int pos = 0;

	list_for_each_entry(list, head, node) {
		int i;

		for (i = 0; i < list->count; i++) {
			unsigned int len = hl_entry_len(&list->entries[i]);

			if (unlikely(pos + len > new_size)) {
				struct list_head *first;

				len = new_size - pos;
				hl_entry_len(&list->entries[i]) = len;
				if (len)
					i++;
				list->count = i;
				first = &list->node;
				if (i)
					first = first->next;
				if (first != head)
					free_rest(head, first);
				return;
			}
			pos += len;
		}
	}
}

void split_hl_list(struct list_head *head, unsigned int offset, struct list_head *other)
{
	struct hl_list *list;
	unsigned int pos = 0;

	list_init(other);
	list_for_each_entry(list, head, node) {
		int count = list->count;
		int i;

		for (i = 0; i < count; i++) {
			struct hl_entry *e = &list->entries[i];
			unsigned int len = hl_entry_len(e);
			struct list_head *node;

			if (likely(pos + len < offset)) {
				pos += len;
				continue;
			}

			if (pos + len == offset && len) {
				pos += len;
				continue;
			}

			node = &list->node;
			if (i || offset != pos) {
				unsigned int left = offset - pos;
				unsigned int right = len - left;

				hl_entry_len(e) = left;
				list->count = i;
				if (left)
					list->count++;

				add_highlight(other, e->desc, right);

				for (i = i + 1; i < count; i++) {
					e = &list->entries[i];
					merge_highlight_entry(other, e);
				}
				node = node->next;
			}

			/* steal complete lists */
			if (node != head) {
				struct list_head *prev = node->prev;
				struct list_head *first = node;
				struct list_head *last = head->prev;

				prev->next = head;
				head->prev = prev;

				other->prev->next = first;
				first->prev = other->prev;

				last->next = other;
				other->prev = last;
			}
			return;
		}
	}
}

static void delete_hl_entries(struct list_head *head, struct hl_list *list, int idx, int pos, unsigned int count)
{
	int i, hole_start = idx;

	if (pos)
		hole_start++;
	while (count) {
		struct list_head *next = list->node.next;
		int nr_zero = 0;

		BUG_ON(&list->node == head);

		i = idx;
		while (count && i < list->count) {
			unsigned int len = hl_entry_len(&list->entries[i]);
			unsigned int del = len - pos;

			BUG_ON(pos > len);

			if (del > count)
				del = count;
			count -= del;
			len -= del;
			if (!len)
				nr_zero++;
			hl_entry_len(&list->entries[i]) = len;

			pos = 0;
			i++;
		}

		list->count -= nr_zero;
		if (!list->count) {
			list_del(&list->node);
			free(list);
		} else if (hole_start < list->count) {
			int di = hole_start;
			int si = hole_start + nr_zero;
			int c = list->count - hole_start;
			memmove(list->entries + di, list->entries + si, c * sizeof(list->entries[0]));
		}
		list = HL_LIST(next);
		idx = 0;
		hole_start = 0;
	}
}

void delete_hl_range(struct list_head *head, unsigned int so, unsigned int eo)
{
	unsigned int pos = 0;
	struct hl_list *list;

	list_for_each_entry(list, head, node) {
		int i;
		for (i = 0; i < list->count; i++) {
			unsigned int len = hl_entry_len(&list->entries[i]);
			unsigned int new_pos = pos + len;

			if (new_pos < so) {
				pos = new_pos;
				continue;
			}

			delete_hl_entries(head, list, i, so - pos, eo - so);
			return;
		}
	}
}

void join_hl_lists(struct list_head *head, struct list_head *other)
{
	struct hl_list *a, *b;
	struct list_head *last;

	if (list_empty(other))
		return;

	if (list_empty(head)) {
		*head = *other;
		return;
	}

	a = HL_LIST(head->prev);
	b = HL_LIST(other->next);

	if (a->count + b->count <= ARRAY_COUNT(a->entries)) {
		int i;
		for (i = 0; i < b->count; i++)
			merge_highlight_entry(head, &b->entries[i]);
		list_del(&b->node);
		free(b);
		if (list_empty(other))
			return;
	}

	last = head->prev;

	other->next->prev = last;
	other->prev->next = head;

	last->next = other->next;
	head->prev = other->prev;
}
